/*
 * Copyright (C) 2004-2026 Metaphonic Labs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * worklet.js -- the synth and the composer scheduler, on the audio thread.
 *
 * One processor, one module, one synth, one transport. The main thread
 * fetches the wasm and posts its bytes here (host.js says why the bytes and
 * not a compiled module); this compiles and instantiates them, since a
 * worklet cannot fetch. After that everything is messages -- load a .dsp or
 * a .gen, a key down, a knob moved, Play -- and process() asks the synth
 * for each 128-frame quantum, which is where the transport is stepped too.
 *
 * What a message means is engine.js's, not this file's: the mirror is
 * another instance of this module fed the same stream, and one switch over
 * message types is what makes "the same stream" mean the same thing on
 * both. What is left here is the worklet's own -- instantiating,
 * rendering, and the tape.
 *
 * The traffic the other way is the tape: what the scheduler delivered, the
 * transport's position, and the epoch a rewind bumps. It goes in batches
 * rather than per quantum -- a quantum is 2.7 ms and a piano roll does not
 * want 375 messages a second -- which costs the page at most TAPE_EVERY
 * quanta of freshness and costs a composed note nothing, since the note
 * sounded here.
 *
 * No SharedArrayBuffer, so none of the cross-origin isolation it demands:
 * two threads, messages between them, which is the shape the desktop
 * already has (docs/JAM.md).
 */

import createThinkWeb from './thinkweb.js';
import { apply } from './engine.js';
import { drain, loadErrors } from './tape.js';

/* How many 128-frame quanta between posts to the page: 43 ms at 48 kHz. */
const TAPE_EVERY = 16;

/* The glue asks for the time now and then; a worklet has no performance
   object to ask. The audio clock is the only clock here anyway. */
globalThis.performance ??= { now: () => currentTime * 1000 };

/* And it asks for entropy. An AudioWorkletGlobalScope is not a window and
   not a worker, so it has no `crypto' either -- and the module reaches for
   one before the first note: thcScheduler's constructor draws its master
   seed from g_random_int, which is std::random_device, which is getentropy.
 *
 * Math.random is enough for what that seed is. It is the seed of a piece
 * that pinned none, whose one requirement is to differ between runs; a
 * piece that pins one never looks. Nothing in this module is cryptographic.
 * (When there are peers, an unpinned seed will have to be agreed rather
 * than drawn -- two peers composing from different seeds are playing
 * different pieces -- but that is a message, and this module has none.) */
globalThis.crypto ??= {
    getRandomValues (view)
    {
        for (let i = 0; i < view.length; i++)
            view[i] = Math.random() * 0x100000000;

        return view;
    },
};

class ThinkProcessor extends AudioWorkletProcessor
{
    constructor ()
    {
        super();

        this.M = null;
        this.aligned = false;   /* the module's frames put on ours */
        this.early = [];        /* messages that arrived before the module */
        this.quanta = 0;        /* since the last post to the page */
        this.events = [];

        /* The probes armed here, by slot, and what each has published
           since the last batch. A probe is a tap on one arg of one node
           of whatever is loaded on a channel; the page displays it. */
        this.probes = new Set();
        this.taps = new Map();

        /* The live input. `micPtr' is where in the heap a quantum of capture
           goes, asked for once (thinkweb.cpp, tw_capture_buffer); `micCap' is
           how many frames fit. Both stay 0 until the module is up. */
        this.micPtr = 0;
        this.micCap = 0;

        /* And how loud it was, since the last batch went to the page.
         *
         * A meter, and it earns its place: the browser's automatic gain
         * control is switched off on purpose (mic.js), so what arrives is
         * whatever the room and the hardware give -- ten to twenty times
         * quieter than the synth channel a vocoder is usually driven by, and
         * different on every machine. The graph's answer is a gain knob, and a
         * gain knob with nothing to aim by is a guess. Computed here, where
         * the samples already are, rather than through a wasm call the page
         * has no thread to make. */
        this.micPeak = 0;
        this.epoch = 0;         /* the epoch this.events belong to */
        this.port.onmessage = (e) => this.receive(e.data);

        /* A message this side could not take -- a module that did not
           survive the trip -- arrives as this rather than as a message, and
           unheard it would leave the page waiting for a `ready' forever. */
        this.port.onmessageerror = () => this.port.postMessage(
            { type: 'error', text: 'a message to the worklet could not be ' +
                                   'received' });
    }

    /* A worklet's errors go nowhere the page can see, so they are sent. */
    async start (m)
    {
        try
        {
            await this.instantiate(m);
        }
        catch (e)
        {
            this.port.postMessage({ type: 'error', text: String(e) });
        }
    }

    async instantiate ({ bytes, windowlen })
    {
        const log = (text) => this.port.postMessage({ type: 'log', text });

        /* The glue would fetch the .wasm itself; handed this, it does not.
           A compile that fails never calls done(), and the glue would wait
           for it for ever, so the failure is reported from here. */
        const M = await createThinkWeb({
            instantiateWasm: (imports, done) =>
            {
                WebAssembly.instantiate(bytes, imports)
                    .then((r) => done(r.instance, r.module))
                    .catch((e) => this.port.postMessage(
                        { type: 'error',
                          text: `the wasm did not compile: ${e}` }));

                return {};
            },
            print: log,
            printErr: log,
        });

        const took = M._tw_create(sampleRate, windowlen, 128);

        /* Asked for once, and once is enough: it is an address inside the
           wasm memory, and growing that appends pages rather than moving
           what is already there. What does change on a grow is the JS view,
           `HEAPF32', which process() re-reads every quantum already. */
        this.micPtr = M._tw_capture_buffer();
        this.micCap = M._tw_capture_capacity();

        this.M = M;
        this.epoch = M._tw_epoch();
        this.port.postMessage({ type: 'ready', windowlen: took, sampleRate });

        for (const m of this.early.splice(0))
            this.receive(m);
    }

    receive (m)
    {
        if (m.type === 'start')
        {
            this.start(m);
            return;
        }

        if (this.M === null)
        {
            this.early.push(m);
            return;
        }

        if (apply(this.M, m, {
                loaded: (id, ok) =>
                    this.port.postMessage({ type: 'loaded', id, ok }),
                /* More than ok: a refusal has a reason, and a patch that
                   loaded has a document the page reads its title off. */
                patched: (id, ok, why, json) =>
                    this.port.postMessage({ type: 'patched', id, ok, why,
                                            json }),
                piece: (id, ok) =>
                    this.port.postMessage({ type: 'piece', id,
                                            ...this.piece(ok) }),
                /* A log and not an `error': that one is the start
                   failing, and rejects the page's promise. */
                log: (text) =>
                    this.port.postMessage({ type: 'log',
                                            text: `worklet: ${text}` }),
            }))
            return;

        /* A parameter panel, read off whatever is on the channel now.
         *
         * A question and not a command, so it is answered rather than
         * applied, and only this instance is asked: what the page draws is
         * what the thing that sounds holds. An edit of one goes the other
         * way, through engine.js, because that is a command.
         *
         * Two answers, and the difference between them is the whole reason
         * a panel carries a shape. `panel' is the description -- the rows,
         * what each is worth, which group it is in -- read once when the
         * panel appears. `panelvalues' is the numbers, polled while it is
         * up, and it carries the shape so that the page knows when the
         * description it drew from has stopped being true.
         */
        if (m.type === 'panel')
        {
            const shape = this.M._tw_panel_open(m.kind, m.a, m.b) >>> 0;

            this.port.postMessage({
                type: 'panel', id: m.id, shape,
                json: shape === 0
                    ? '' : this.M.UTF8ToString(this.M._tw_panel_json()),
            });

            return;
        }

        if (m.type === 'panelvalues')
        {
            /* The shape first: it rebuilds from live state, which is what
               makes the values below the current ones. */
            const shape = this.M._tw_panel_shape() >>> 0;
            const values = [];

            for (let row = 0; row < m.rows; row++)
                values.push(this.M._tw_panel_value(row));

            this.port.postMessage({ type: 'panelvalues', id: m.id, shape,
                                    values });

            return;
        }

        /* What is on a channel and whether it has been edited since it
         * was read.
         *
         * A question, like the panel above and for the same reason: what
         * the page draws is what the thing that sounds holds. The edits
         * that make it dirty are commands and have already been applied on
         * both instances by the time anybody asks.
         */
        if (m.type === 'patchstate')
        {
            this.port.postMessage({
                type: 'patchstate', id: m.id,
                json: this.M.ccall('tw_patch_json', 'string', ['number'],
                                   [m.channel]),
            });

            return;
        }

        /* The piece's text with its `tempo' statement set.
         *
         * Made here because this is where the document is -- tw_piece_load
         * wrote it, and thcGenEdit reads and writes the file it wrote --
         * and made in the mirror too, which holds its own copy and is the
         * one the canvas describes (mirror.js). The scheduler is not
         * touched: what is heard came from the stamped tempo command, and
         * this is the other half, which is the text a reload would come
         * back at.
         *
         * "" when the edit was refused, which the page reads as "leave the
         * box alone". */
        /* One stage's param set in a text that is not this instance's
         * piece: the edit a room page's own peer made, applied to the
         * document as it now stands. "" when the writer refused it. */
        if (m.type === 'genparam')
        {
            this.port.postMessage({
                type: 'genparam', id: m.id,
                text: this.M.ccall('tw_gen_set_param', 'string',
                                   ['string', 'string', 'number', 'string',
                                    'string'],
                                   [m.text, m.chain, m.stage, m.param,
                                    m.valueText]),
            });

            return;
        }

        if (m.type === 'settempo')
        {
            this.port.postMessage({
                type: 'settempo', id: m.id,
                text: this.M.ccall('tw_piece_set_tempo', 'string',
                                   ['number'], [m.bpm]),
            });

            return;
        }

        /* The bytes a Save would write for a channel, and the note that
         * they were kept.
         *
         * A question and then a command, because that is what they are: a
         * page that asked what a Save would write and then thought better
         * of it has not saved anything. Both of this instance, which is
         * the one that sounds and therefore the one whose values are the
         * ones worth writing down.
         */
        if (m.type === 'patchcompose')
        {
            this.port.postMessage({
                type: 'patchcompose', id: m.id,
                text: this.M.ccall('tw_patch_compose', 'string',
                                   ['number', 'string'],
                                   [m.channel, m.stamp ?? '']),
            });

            return;
        }

        if (m.type === 'patchsaved')
        {
            this.M.ccall('tw_patch_saved', 'number', ['number', 'string'],
                         [m.channel, m.name ?? '']);

            return;
        }

        if (m.type === 'piecefile')
        {
            this.M.ccall('tw_gen_file', 'number', ['string', 'string'],
                         [m.name, m.text]);

            return;
        }

        /* The catalog the page's piece menu is drawn from: every .gen
           handed over above, by the category each declares. Read through
           thcGenEdit, which is what the Composer edits a piece with, so
           the menu here and the Composer's Open are the same list. */
        if (m.type === 'gens')
        {
            this.port.postMessage({
                type: 'gens', id: m.id,
                catalog: JSON.parse(this.M.ccall('tw_gens_json', 'string',
                                                 [], [])),
            });

            return;
        }

        /* The catalog the page's instrument menus are drawn from: every
           .dsp this module has been handed, by group, with the title and
           the description its author wrote. The module scans its own copy
           of them -- they are files here, under /dsp -- so this is the
           reading the desktop does, not a second one in JavaScript. */
        if (m.type === 'dsps')
        {
            this.port.postMessage({
                type: 'dsps', id: m.id,
                catalog: JSON.parse(this.M.ccall('tw_dsps_json', 'string',
                                                 [], [])),
            });

            return;
        }

        /* The first-run configuration: what belongs on a channel nothing
           has aimed, and how many distinct answers there are. The rule is
           the module's (src/PatchSet.h); the page fetches what it names. */
        if (m.type === 'patchdefaults')
        {
            const names = [];

            for (let c = 0; c < this.M._tw_patch_default_count(); c++)
                names.push(this.M.ccall('tw_patch_default', 'string',
                                        ['number'], [c]));

            this.port.postMessage({ type: 'patchdefaults', id: m.id, names });

            return;
        }

        if (m.type === 'patchdefault')
        {
            this.port.postMessage({
                type: 'patchdefault', id: m.id,
                name: this.M.ccall('tw_patch_default', 'string', ['number'],
                                   [m.channel]),
            });

            return;
        }

        /* A tap, armed on this thread -- which is the GUI thread and the
           audio thread at once here, so the call that resolves the node
           and the call that drains the ring are on the same one. */
        if (m.type === 'probe')
        {
            const slot = this.M.ccall('tw_probe_arm', 'number',
                                      ['number', 'string', 'string'],
                                      [m.channel, m.node, m.arg]);

            if (slot >= 0)
                this.probes.add(slot);

            this.port.postMessage({
                type: 'probed', id: m.id, slot,
                why: this.M.UTF8ToString(this.M._tw_probe_why()),
            });

            return;
        }

        if (m.type === 'unprobe')
        {
            this.M._tw_probe_disarm(m.slot);
            this.probes.delete(m.slot);
            this.taps.delete(m.slot);
            return;
        }

        /* The worklet's own. Everything sent before this has been
           handled -- and everything delivered before it has been posted,
           which is the half a tape needs: the batch in hand may be short
           of TAPE_EVERY and would otherwise wait for a quantum that is
           not coming. */
        if (m.type === 'ping')
        {
            this.postTape();
            this.port.postMessage({ type: 'pong', id: m.id });
        }
    }

    /* What the page needs to draw a piece: its name, and the number and
       name of each knob it declared.
     *
       Two fields, where there were seven. What a knob *is* -- its label,
       its range, the resolution worth showing it at -- is a panel now
       (src/KnobPanel.cpp), asked for like any other and drawn by panel.js;
       what is left here is the pairing a command needs, which is the one
       thing a panel row is not enough for on its own. A page holding a
       peer's `knob 3' has to find row "3"; a preset naming `@warmth' has to
       find the number. */
    piece (ok)
    {
        if (!ok)
            return { errors: loadErrors(this.M), name: '', description: '',
                     tempo: 0, beats: false,
                     knobs: [], instruments: [], listens: [], sinks: [] };

        /* Off the panel, so there is one answer to "which knobs are shown"
           rather than two that have to agree. Hidden knobs keep their
           numbers and get no row. */
        const knobs = [];

        if (this.M._tw_panel_open(1 /* thPanel::KNOB */, 0, 0) !== 0)
            for (const row of JSON.parse(
                     this.M.UTF8ToString(this.M._tw_panel_json())).rows)
                knobs.push({ knob: Number(row.id), name: row.knob });

        /* The instruments by name with the channel each was given, which
           is what a seat is; and the channels the piece takes `input
           midi' on, which is where a seat's keys go into the piece rather
           than straight onto its channel. */
        const instruments = [];

        for (let i = 0; i < this.M._tw_instrument_count(); i++)
            instruments.push({
                name: this.M.UTF8ToString(this.M._tw_instrument_name(i)),
                /* The .dsp it plays, which is what ties a file in the
                   document to a channel in the synth. */
                dsp: this.M.UTF8ToString(this.M._tw_instrument_dsp(i)),
                channel: this.M._tw_instrument_channel(i),
            });

        const listens = [];

        for (let c = 0; c < 16; c++)
            if (this.M._tw_listens(c))
                listens.push(c);

        /* And the channels its sinks name that it put no instrument of
           its own on: the ones the page has to aim, or the piece is
           composed and nothing sounds. */
        const sinks = [];

        for (let i = 0; i < this.M._tw_sink_count(); i++)
            sinks.push(this.M._tw_sink_channel(i));

        return {
            errors: [],
            name: this.M.UTF8ToString(this.M._tw_piece_name()),
            description: this.M.UTF8ToString(this.M._tw_piece_description()),
            seeded: this.M._tw_piece_seeded() !== 0,
            seed: this.M._tw_seed(),

            /* What it is running at, and whether that reaches it: the
               tempo scales beat-valued durations and nothing else, so a
               piece written in seconds is one the control cannot move.
               The page offers it where it means something, which is the
               rule ComposerWindow follows with the same question. */
            tempo: this.M._tw_tempo(),
            beats: this.M._tw_uses_beats() !== 0,

            /* And how fast the clock is running, which a load does not
               reset: it is what the listener asked for rather than
               anything this piece says. */
            speed: this.M._tw_speed_now(),
            knobs,
            instruments,
            listens,
            sinks,
        };
    }

    process (inputs, outputs)
    {
        const out = outputs[0];

        if (this.M === null || out.length === 0)
            return true;

        /* The module's frame counter starts at zero when tw_create ran;
           this context's is already well past zero by then, and every
           frame the page hands in -- a Play's origin above all -- is in
           the context's numbering. One call before the first render puts
           the two on one counter (thinkweb.cpp, tw_align). */
        if (!this.aligned)
        {
            this.M._tw_align(currentFrame);
            this.aligned = true;

            /* Said once, because the mirror has to start counting frames
               where this does or the frame in a tape batch means nothing
               to it. */
            this.port.postMessage({ type: 'aligned', frame: currentFrame });
        }

        const frames = out[0].length;

        this.feedInput(inputs[0], frames);

        const p = this.M._tw_render(frames) >> 2;
        const heap = this.M.HEAPF32;

        /* Interleaved stereo in the heap; planar channels out. A mono
           destination gets the left. */
        for (let c = 0; c < out.length; c++)
        {
            const ch = out[c];
            const src = p + (c < 2 ? c : 1);

            for (let i = 0; i < frames; i++)
                ch[i] = heap[src + i * 2];
        }

        /* A rewind or a load inside the batch: what is held so far is
           the old run's, and goes out under the old run's epoch before
           anything from the new one joins it -- posted together, the page
           would clear its roll for the new epoch and then draw the old
           notes into it. */
        const epoch = this.M._tw_epoch();

        if (epoch !== this.epoch)
        {
            if (this.events.length > 0)
                this.postTape();

            this.epoch = epoch;
        }

        /* Drained every quantum and posted every TAPE_EVERY: the module
           holds the events until somebody takes them, and letting a minute
           of a busy piece pile up there would be a megabyte nobody asked
           for. */
        drain(this.M, this.events);
        this.drainProbes();
        this.postParamEdits();

        if (++this.quanta >= TAPE_EVERY)
            this.postTape();

        return true;
    }

    /* The stage params this quantum wrote to the piece, and the piece as
     * they left it.
     *
     * The file is this instance's, and what the next load reads is the
     * page's -- its box, or a room's document -- so the page is told and
     * writes it there. Posted as they land rather than with the tape: an
     * edit is one keystroke, and the page would otherwise sit on a
     * document it knows is stale for up to a batch. */
    postParamEdits ()
    {
        if (this.M._tw_param_edit_count() === 0)
            return;

        this.port.postMessage({
            type: 'paramedits',
            edits: JSON.parse(this.M.ccall('tw_param_edits_json', 'string')),
            piece: this.M.ccall('tw_piece_text', 'string'),
        });
    }

    /* This quantum's capture into the heap, summed to mono, and handed over
     * before the render -- which is where a duplex callback has it, and what
     * makes the graph's `live0' this quantum's microphone rather than a
     * message that arrived at some point.
     *
     * A worklet input that nothing is connected to arrives as an **empty
     * array**, not a buffer of zeros, so "no mic" is a branch and not an
     * accident. Not calling tw_capture at all is the right thing for it: the
     * accumulator hears the gap and feeds the graph silence, rather than the
     * last window over and over (gthSynthSource).
     *
     * Summed and then scaled by the channel count, which is what the wav
     * reader does to a stereo file and for the same reason: two sides
     * carrying one signal come out at the level they went in.
     */
    feedInput (channels, frames)
    {
        if (this.micPtr === 0 || !channels || channels.length === 0)
            return;

        const n = Math.min(frames, this.micCap, channels[0].length);

        if (n === 0)
            return;

        const heap = this.M.HEAPF32;
        const at = this.micPtr >> 2;
        const count = channels.length;

        if (count === 1)
        {
            heap.set(channels[0].subarray(0, n), at);
        }
        else
        {
            for (let i = 0; i < n; i++)
            {
                let sum = 0;

                for (let c = 0; c < count; c++)
                    sum += channels[c][i];

                heap[at + i] = sum / count;
            }
        }

        /* Off the heap rather than off `channels', so the meter reads what the
           graph is about to read and not what arrived: a page showing a level
           the engine disagrees with is worse than no page. */
        for (let i = 0; i < n; i++)
        {
            const v = heap[at + i] < 0 ? -heap[at + i] : heap[at + i];

            if (v > this.micPeak)
                this.micPeak = v;
        }

        this.M._tw_capture(n);
    }

    /* What each armed tap has published since the last quantum, copied
       out of the heap. The ring is the synth's and is overwritten; these
       are held until the next batch goes to the page.
     *
       Eight probes at a couple of thousand samples is 64 KB a batch and a
       megabyte and a half a second at the very most -- and nothing at all
       when none is armed, which is the usual case. */
    drainProbes ()
    {
        for (const slot of this.probes)
        {
            const got = this.M._tw_probe_read(slot);

            if (got === 0)
                continue;

            const at = this.M._tw_probe_samples() >> 2;
            const samples = this.M.HEAPF32.slice(at, at + got);
            const had = this.taps.get(slot);

            if (had === undefined)
            {
                this.taps.set(slot, samples);
                continue;
            }

            /* Two quanta in one batch: the older first, since a visual
               module is fed a signal and not a set of windows. */
            const both = new Float32Array(had.length + samples.length);

            both.set(had);
            both.set(samples, had.length);
            this.taps.set(slot, both);
        }
    }

    postTape ()
    {
        this.quanta = 0;
        /* `frame' is where this synth's output has got to, `origin' where
           its transport zero is and `speed' how many transport seconds a
           second of output carries, so the page can turn a transport time
           into a frame and back; `late' is how many commands have been
           applied after their time (thinkweb.cpp). The speed is here
           because the two numbers are the whole of that line: a clock
           that read the origin and assumed the speed would be wrong by
           the factor the slider was moved to. */
        this.port.postMessage({
            type: 'tape',
            now: this.M._tw_now(),
            epoch: this.epoch,
            running: this.M._tw_running() !== 0,
            frame: this.M._tw_frame(),
            origin: this.M._tw_origin(),
            speed: this.M._tw_speed_now(),
            late: this.M._tw_late(),

            /* The loudest capture frame since the last batch, and what the
               accumulator had to throw away. A peak held over a batch rather
               than an instantaneous reading, because what somebody setting a
               gain wants to see is how loud they got. */
            capture: this.micPeak,
            captureDropped: this.M._tw_capture_dropped(),
            events: this.events,
            probes: [...this.taps].map(([slot, samples]) => ({ slot,
                                                               samples })),
        }, [...this.taps.values()].map((s) => s.buffer));
        this.events = [];
        this.taps.clear();
        this.micPeak = 0;
    }
}

registerProcessor('thinksynth', ThinkProcessor);
