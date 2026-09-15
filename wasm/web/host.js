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
 * host.js -- the main thread's side of the synth.
 *
 * createSynth() puts worklet.js on an AudioContext -- a live one for the
 * page, an OfflineAudioContext for browsertest.mjs -- and hands back the few
 * things it understands. The wasm is fetched here, since a worklet cannot
 * fetch, and its bytes are posted over for the worklet to compile.
 *
 * The bytes, not a compiled WebAssembly.Module. The plan was to compile
 * here and post the module (JAM.md, section 3), and Firefox takes one --
 * but Chrome cannot receive a Module on an AudioWorklet's port: it arrives
 * as a messageerror and the worklet never starts. Bytes cross everywhere,
 * and compiling a quarter of a megabyte before the first note is not a
 * cost anyone hears.
 *
 * A note carries the frame it applies at (thinkweb.cpp); -1, the default,
 * is "the next window", which is how a key pressed now is played.
 */

let fetched = null;

function wasmBytes ()
{
    fetched ??= fetch(new URL('thinkweb.wasm', import.meta.url))
        .then((r) => r.arrayBuffer());

    return fetched;
}

export async function createSynth (ctx, { windowlen = 256,
                                          onLog = () => {} } = {})
{
    const [bytes] = await Promise.all([
        wasmBytes(),
        ctx.audioWorklet.addModule(new URL('worklet.js', import.meta.url)),
    ]);

    const node = new AudioWorkletNode(ctx, 'thinksynth', {
        numberOfInputs: 0,
        numberOfOutputs: 1,
        outputChannelCount: [2],
    });

    /* Replies by id: a load says whether it parsed, a ping that everything
       sent before it has been handled. */
    const waiting = new Map();
    let nextId = 0;
    let ready, failed;
    const isReady = new Promise((resolve, reject) =>
    {
        ready = resolve;
        failed = reject;
    });

    node.port.onmessage = (e) =>
    {
        const m = e.data;

        switch (m.type)
        {
            case 'log':
                onLog(m.text);
                break;
            case 'ready':
                ready(m);
                break;
            case 'error':
                onLog(`worklet: ${m.text}`);
                failed(new Error(`the worklet could not start: ${m.text}`));
                break;
            case 'loaded':
            case 'pong':
                waiting.get(m.id)?.(m.ok);
                waiting.delete(m.id);
                break;
        }
    };

    const ask = (message) => new Promise((resolve) =>
    {
        const id = nextId++;

        waiting.set(id, resolve);
        node.port.postMessage({ ...message, id });
    });

    /* A copy, transferred: the transfer empties what it sends, and the
       next synth on the page needs the bytes too. */
    const copy = bytes.slice(0);

    node.port.postMessage({ type: 'start', bytes: copy, windowlen }, [copy]);

    const info = await isReady;

    return {
        node,
        windowlen: info.windowlen,
        sampleRate: info.sampleRate,

        /* Resolves true if the .dsp parsed. */
        load: (text) => ask({ type: 'load', text }),

        noteOn: (note, velocity = 100, frame = -1) =>
            node.port.postMessage({ type: 'on', note, velocity, frame }),

        noteOff: (note, frame = -1) =>
            node.port.postMessage({ type: 'off', note, frame }),

        allOff: () => node.port.postMessage({ type: 'alloff' }),

        /* Resolves once the worklet has handled everything sent so far. */
        flush: () => ask({ type: 'ping' }),
    };
}
