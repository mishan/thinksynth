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

    /* What the page needs to draw a piece: its name, and the knobs it
       declared, with the range and label each was given. Read once, at the
       load -- a knob's value moves, but nothing else about it does. */
    piece (ok)
    {
        if (!ok)
            return { errors: loadErrors(this.M), name: '', description: '',
                     knobs: [], instruments: [], listens: [], sinks: [] };

        /* `knob' is the index a command names it by; the list is every
           knob the piece declared, hidden ones included, so the index is
           the module's own. */
        const knobs = [];

        for (let i = 0; i < this.M._tw_knob_count(); i++)
            if (this.M._tw_knob_shown(i))
                knobs.push({
                    knob:  i,
                    name:  this.M.UTF8ToString(this.M._tw_knob_name(i)),
                    label: this.M.UTF8ToString(this.M._tw_knob_label(i)),
                    min:   this.M._tw_knob_min(i),
                    max:   this.M._tw_knob_max(i),
                    step:  this.M._tw_knob_step(i),
                    value: this.M._tw_knob_value(i),
                });

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

        if (++this.quanta >= TAPE_EVERY)
            this.postTape();

        return true;
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
        /* `frame' is where this synth's output has got to and `origin'
           where its transport zero is, so the page can turn a transport
           time into a frame and back; `late' is how many commands have
           been applied after their time (thinkweb.cpp). */
        this.port.postMessage({
            type: 'tape',
            now: this.M._tw_now(),
            epoch: this.epoch,
            running: this.M._tw_running() !== 0,
            frame: this.M._tw_frame(),
            origin: this.M._tw_origin(),
            late: this.M._tw_late(),
            events: this.events,
            probes: [...this.taps].map(([slot, samples]) => ({ slot,
                                                               samples })),
        }, [...this.taps.values()].map((s) => s.buffer));
        this.events = [];
        this.taps.clear();
    }
}

registerProcessor('thinksynth', ThinkProcessor);
