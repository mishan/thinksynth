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
 * worklet.js -- the synth, on the audio thread.
 *
 * One processor, one module, one synth. The main thread fetches the wasm
 * and posts its bytes here (host.js says why the bytes and not a compiled
 * module); this compiles and instantiates them, since a worklet cannot
 * fetch. After that everything is messages -- load a .dsp, a key down, a
 * key up -- and process() asks the synth for each 128-frame quantum.
 *
 * No SharedArrayBuffer, so none of the cross-origin isolation it demands:
 * two threads, messages between them, which is the shape the desktop
 * already has (JAM.md, section 3).
 */

import createThinkWeb from './thinkweb.mjs';

/* The glue asks for the time now and then; a worklet has no performance
   object to ask. The audio clock is the only clock here anyway. */
globalThis.performance ??= { now: () => currentTime * 1000 };

class ThinkProcessor extends AudioWorkletProcessor
{
    constructor ()
    {
        super();

        this.M = null;
        this.early = [];        /* messages that arrived before the module */
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
                const ok = this.M.ccall('tw_load', 'number', ['string'],
                                        [m.text]) !== 0;

                this.port.postMessage({ type: 'loaded', id: m.id, ok });
                break;
            }
            case 'on':
                this.M._tw_note_on(m.frame, m.note, m.velocity);
                break;
            case 'off':
                this.M._tw_note_off(m.frame, m.note);
                break;
            case 'alloff':
                this.M._tw_all_off();
                break;
            case 'ping':
                this.port.postMessage({ type: 'pong', id: m.id });
                break;
        }
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

        return true;
    }
}

registerProcessor('thinksynth', ThinkProcessor);
