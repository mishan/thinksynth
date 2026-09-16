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
 * The traffic the other way is the tape: what the scheduler delivered, the
 * transport's position, and the epoch a rewind bumps. It goes in batches
 * rather than per quantum -- a quantum is 2.7 ms and a piano roll does not
 * want 375 messages a second -- which costs the page at most TAPE_EVERY
 * quanta of freshness and costs a composed note nothing, since the note
 * sounded here.
 *
 * No SharedArrayBuffer, so none of the cross-origin isolation it demands:
 * two threads, messages between them, which is the shape the desktop
 * already has (JAM.md, section 3).
 */

import createThinkWeb from './thinkweb.mjs';
import { drain } from './tape.mjs';

/* How many 128-frame quanta between posts to the page: 43 ms at 48 kHz. */
const TAPE_EVERY = 16;

/* thinkweb.cpp's TransportOp. */
const TRANSPORT = { start: 0, stop: 1, rewind: 2, tempo: 3 };

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
 * different pieces -- but that is a message, and messages are M3.) */
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
        this.early = [];        /* messages that arrived before the module */
        this.quanta = 0;        /* since the last post to the page */
        this.events = [];
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

        switch (m.type)
        {
            case 'load':
            {
                const ok = this.M.ccall('tw_load', 'number',
                                        ['number', 'string'],
                                        [m.channel, m.text]) !== 0;

                this.port.postMessage({ type: 'loaded', id: m.id, ok });
                break;
            }
            case 'instrument':
                this.M.ccall('tw_instrument', 'number', ['string', 'string'],
                             [m.name, m.text]);
                break;
            case 'piece':
            {
                const ok = this.M.ccall('tw_piece_load', 'number', ['string'],
                                        [m.text]) !== 0;

                this.port.postMessage({ type: 'piece', id: m.id,
                                        ...this.piece(ok) });
                break;
            }
            case 'transport':
                /* Checked, because an op this does not know would reach
                   the module as undefined, arrive as zero and start the
                   transport -- a typo that plays the piece. */
                if (!(m.op in TRANSPORT))
                {
                    /* A log and not an `error': that one is the start
                       failing, and rejects the page's promise. */
                    this.port.postMessage(
                        { type: 'log',
                          text: `worklet: no transport op '${m.op}'` });
                    break;
                }

                this.M._tw_transport(m.frame, TRANSPORT[m.op], m.value ?? 0);
                break;
            case 'knob':
                this.M.ccall('tw_knob', null, ['number', 'string', 'number'],
                             [m.frame, m.name, m.value]);
                break;
            case 'midion':
                this.M._tw_midi_on(m.frame, m.channel, m.note, m.velocity);
                break;
            case 'midioff':
                this.M._tw_midi_off(m.frame, m.channel, m.note);
                break;
            case 'on':
                this.M._tw_note_on(m.frame, m.channel, m.note, m.velocity);
                break;
            case 'off':
                this.M._tw_note_off(m.frame, m.channel, m.note);
                break;
            case 'alloff':
                this.M._tw_all_off();
                break;
            case 'ping':
                /* Everything sent before this has been handled -- and
                   everything delivered before it has been posted, which is
                   the half a tape needs: the batch in hand may be short of
                   TAPE_EVERY and would otherwise wait for a quantum that
                   is not coming. */
                this.postTape();
                this.port.postMessage({ type: 'pong', id: m.id });
                break;
        }
    }

    /* What the page needs to draw a piece: its name, and the knobs it
       declared, with the range and label each was given. Read once, at the
       load -- a knob's value moves, but nothing else about it does. */
    piece (ok)
    {
        if (!ok)
        {
            const errors = [];

            for (let i = 0; i < this.M._tw_error_count(); i++)
                errors.push(this.M.UTF8ToString(this.M._tw_error(i)));

            return { errors, name: '', description: '', knobs: [] };
        }

        const knobs = [];

        for (let i = 0; i < this.M._tw_knob_count(); i++)
            if (this.M._tw_knob_shown(i))
                knobs.push({
                    name:  this.M.UTF8ToString(this.M._tw_knob_name(i)),
                    label: this.M.UTF8ToString(this.M._tw_knob_label(i)),
                    min:   this.M._tw_knob_min(i),
                    max:   this.M._tw_knob_max(i),
                    value: this.M._tw_knob_value(i),
                });

        return {
            errors: [],
            name: this.M.UTF8ToString(this.M._tw_piece_name()),
            description: this.M.UTF8ToString(this.M._tw_piece_description()),
            knobs,
        };
    }

    process (inputs, outputs)
    {
        const out = outputs[0];

        if (this.M === null || out.length === 0)
            return true;

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

        /* Drained every quantum and posted every TAPE_EVERY: the module
           holds the events until somebody takes them, and letting a minute
           of a busy piece pile up there would be a megabyte nobody asked
           for. */
        drain(this.M, this.events);

        if (++this.quanta >= TAPE_EVERY)
            this.postTape();

        return true;
    }

    postTape ()
    {
        this.quanta = 0;
        this.port.postMessage({
            type: 'tape',
            now: this.M._tw_now(),
            epoch: this.M._tw_epoch(),
            running: this.M._tw_running() !== 0,
            events: this.events,
        });
        this.events = [];
    }
}

registerProcessor('thinksynth', ThinkProcessor);
