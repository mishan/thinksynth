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
 * Everything the page does to the synth carries the frame it applies at
 * (thinkweb.cpp); -1, the default, is "the next window", which is how a key
 * pressed now is played. A knob and a transport button carry one too: the
 * page is the nearest peer and not a privileged one, and when there are
 * other peers they will send the same commands with the same stamps
 * (JAM.md, section 3).
 */

let fetched = null;

function wasmBytes ()
{
    fetched ??= fetch(new URL('thinkweb.wasm', import.meta.url))
        .then((r) =>
        {
            if (!r.ok)
                throw new Error(`thinkweb.wasm: ${r.status} ${r.statusText}`);

            return r.arrayBuffer();
        })
        .catch((e) =>
        {
            /* Not kept: Start after a failed fetch fetches again. */
            fetched = null;
            throw e;
        });

    return fetched;
}

export async function createSynth (ctx, { windowlen = 256,
                                          onLog = () => {},
                                          onTape = () => {} } = {})
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
            case 'piece':
                waiting.get(m.id)?.(m);
                waiting.delete(m.id);
                break;
            case 'tape':
                onTape(m);
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

        /* Resolves true if the .dsp parsed. The channel is the caller's:
           a piece that routes `input midi' to a channel it declares no
           instrument for needs something put there to sound. */
        load: (text, channel = 0) => ask({ type: 'load', text, channel }),

        /* A .dsp under the name a piece's `instrument { dsp = ... }' will
           ask for. A worklet cannot fetch, so the page hands these over. */
        instrument: (name, text) =>
            node.port.postMessage({ type: 'instrument', name, text }),

        /* Resolves to what the piece is: its name, its description and the
           knobs it declared -- or just `errors', which is the loader's own
           complaints with line numbers, when it did not parse. */
        loadPiece: (text) => ask({ type: 'piece', text }),

        /* 'start', 'stop', 'rewind' or 'tempo', the last with a value in
           beats per minute. */
        transport: (op, value = 0, frame = -1) =>
            node.port.postMessage({ type: 'transport', op, value, frame }),

        knob: (name, value, frame = -1) =>
            node.port.postMessage({ type: 'knob', name, value, frame }),

        /* A key, into the piece rather than straight onto a channel: the
           chains that declared `input midi' and sink to this channel
           receive it.
         *
           The channel comes after the frame, here and below, because the
           frame was here first and a caller that had learned to put it
           third would have gone on putting it third -- silently sending a
           stamp as a channel and playing every note at once. Which is what
           happened, and what browsertest.mjs caught. */
        midiOn: (note, velocity = 100, frame = -1, channel = 0) =>
            node.port.postMessage({ type: 'midion', note, velocity,
                                    frame, channel }),

        midiOff: (note, frame = -1, channel = 0) =>
            node.port.postMessage({ type: 'midioff', note, frame, channel }),

        noteOn: (note, velocity = 100, frame = -1, channel = 0) =>
            node.port.postMessage({ type: 'on', note, velocity, frame,
                                    channel }),

        noteOff: (note, frame = -1, channel = 0) =>
            node.port.postMessage({ type: 'off', note, frame, channel }),

        allOff: () => node.port.postMessage({ type: 'alloff' }),

        /* Resolves once the worklet has handled everything sent so far. */
        flush: () => ask({ type: 'ping' }),
    };
}
