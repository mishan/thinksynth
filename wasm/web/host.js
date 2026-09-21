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
 * The bytes, not a compiled WebAssembly.Module. The plan was to compile here
 * and post the module (docs/JAM.md), and Firefox takes one -- but Chrome
 * cannot receive a Module on an AudioWorklet's port: it arrives as a
 * messageerror and the worklet never starts. Bytes cross everywhere, and
 * compiling a quarter of a megabyte before the first note is not a cost
 * anyone hears.
 *
 * THE MIRROR, when the caller asks for one: a worker holding the same
 * module with a synth that never renders, fed every message this posts to
 * the worklet and stepped to the frame each tape batch reached. It is
 * where the composer view's real composer instances live, and it draws
 * them (mirror.js). Everything it says comes back through `onMirror';
 * anything the page wants to tell it -- the view's size, a pointer, a
 * request for a frame -- goes through `toMirror'.
 *
 * The tee is here and not at each call site on purpose: "the mirror is fed
 * the messages the worklet is fed" is then a property of one function
 * rather than a promise twenty callers keep.
 *
 * Everything the page does to the synth carries the point it applies at
 * (thinkweb.cpp); -1, the default, is "the next window", which is how a key
 * pressed now is played. A key carries a frame. A knob, a stop and a tempo
 * carry a transport time and are applied inside the step at that time, and a
 * begin carries the frame its transport zero falls on: the page is the
 * nearest peer and not a privileged one, and the other peers send the same
 * commands with the same stamps (docs/JAM.md).
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
                                          onTape = () => {},
                                          onMirror = null } = {})
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

    /* The second port, when the caller wants a mirror. Made before the
       worklet is ready so that nothing posted in between is lost: the
       worker holds what arrives until its own module is up. */
    const mirror = onMirror !== null
        ? new Worker(new URL('mirror.js', import.meta.url), { type: 'module' })
        : null;

    if (mirror !== null)
        mirror.onmessage = (e) => onMirror(e.data);

    /* One message, both ports. See the top of this file. */
    const post = (m) =>
    {
        node.port.postMessage(m);
        mirror?.postMessage(m);
    };

    node.port.onmessage = (e) =>
    {
        const m = e.data;

        switch (m.type)
        {
            /* The frame the worklet put its counter on, once, so the
               mirror can put its own on the same one. */
            case 'aligned':
                mirror?.postMessage({ type: 'align', frame: m.frame });
                break;
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
            case 'probed':
                waiting.get(m.id)?.({ slot: m.slot, why: m.why });
                waiting.delete(m.id);
                break;
            case 'piece':
            case 'panel':
            case 'panelvalues':
                waiting.get(m.id)?.(m);
                waiting.delete(m.id);
                break;
            case 'tape':
                /* And the mirror is told how far this has got: it steps
                   to there, which is tw_render without the render. So its
                   picture is one batch behind the ear, which is about the
                   desktop's 50 ms draw timer. */
                mirror?.postMessage({ type: 'step', frame: m.frame });
                onTape(m);
                break;
        }
    };

    /* Asked of the worklet, whose answer is the one that sounds, and
       told to the mirror, which needs the load as much as the worklet
       does and has nobody waiting on its answer. */
    const ask = (message) => new Promise((resolve) =>
    {
        const id = nextId++;

        waiting.set(id, resolve);
        post({ ...message, id });
    });

    /* A copy, transferred: the transfer empties what it sends, and the
       next synth on the page needs the bytes too. */
    const copy = bytes.slice(0);

    node.port.postMessage({ type: 'start', bytes: copy, windowlen }, [copy]);

    const info = await isReady;

    /* The mirror's own copy of the bytes, and the two numbers the synth
       it makes has to agree with this one about. */
    if (mirror !== null)
    {
        const forMirror = bytes.slice(0);

        mirror.postMessage({ type: 'start', bytes: forMirror,
                             windowlen: info.windowlen,
                             sampleRate: info.sampleRate }, [forMirror]);
    }

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
        instrument: (name, text) => post({ type: 'instrument', name, text }),

        /* And a wav under the name an osc::sample node's `file' will ask
           for, which is `samples/kick909.wav' -- the index.json entry,
           since that is the path and not the basename. Bytes, because it
           is not text. */
        sample: (name, bytes) => post({ type: 'sample', name, bytes }),

        /* One chanarg of whatever is loaded on a channel, at the value a
           .patch overrides it to. The other half of load(), in that
           order: patch.js does the two together, as
           gthPatchManager::parse does. A name the tree does not declare
           is ignored and said once in the log. */
        chanarg: (channel, name, values) =>
            post({ type: 'chanarg', channel, name,
                   values: Array.isArray(values) ? values : [values] }),

        /* Resolves to what the piece is: its name, its description, the
           knobs it declared, its instruments with their channels, the
           channels it listens on, the channels its sinks name that it
           aimed at no instrument of its own, and the seed it composes
           from -- or just
           `errors', which is the loader's own complaints with line
           numbers, when it did not parse. `seed' is the master seed to
           compose from when the file pins none; left out, one is drawn. */
        loadPiece: (text, seed = -1) => ask({ type: 'piece', text, seed }),

        /* 'start', 'stop', 'rewind' or 'tempo', the last with a value in
           beats per minute, at a frame; -1 is the next window. 'start'
           resumes from where the transport is. */
        transport: (op, value = 0, frame = -1) =>
            post({ type: 'transport', op, value, frame }),

        /* From the top, with transport zero at `frame' exactly: what a
           room's Play is, on every peer, at the frame its origin falls
           on. */
        begin: (frame) => post({ type: 'begin', frame }),

        /* 'stop' or 'tempo' at a transport time, applied inside the step
           at that time; -1 is the next window. */
        transportAt: (op, at = -1, value = 0) =>
            post({ type: 'at', op, at, value }),

        /* `knob' is the index loadPiece reported the knob under; `at' a
           transport time, or -1 for the next window. */
        knob: (knob, value, at = -1) => post({ type: 'knob', knob, value, at }),

        /* A channel's parameters, as the module describes them
         * (src/PanelModel.h): `{ shape, json }', and a shape of 0 for a
         * channel with nothing on it. `kind' is thPanel::Kind, `a' the
         * channel, `b' nonzero for the channel effect's arg map rather
         * than the instrument's.
         *
         * Asked of the worklet alone, and deliberately: what the page
         * draws is what the thing that sounds holds. */
        panel: (kind, a, b = 0) =>
            new Promise((resolve) =>
            {
                const id = nextId++;

                waiting.set(id, resolve);
                node.port.postMessage({ type: 'panel', id, kind, a, b });
            }),

        /* The values of the panel that is open, and its shape again.
         *
         * The cheap half: a panel follows the arg -- a knob, a peer, a
         * MIDI controller all write behind it -- and this is what the page
         * polls to keep up. Nothing is serialized and no row is described
         * again; a shape that has changed is the page's cue to ask for the
         * description afresh. */
        panelValues: (rows) =>
            new Promise((resolve) =>
            {
                const id = nextId++;

                waiting.set(id, resolve);
                node.port.postMessage({ type: 'panelvalues', id, rows });
            }),

        /* One row of a panel, set to what somebody typed or dragged it to,
         * as the authored spelling rather than a number -- "4000",
         * "Square". The module folds it, holds it to the row's range and
         * refuses what it cannot make sense of.
         *
         * A command: posted to every instance, applied by each, including
         * this page's own (docs/JAM.md). Which is why the panel it names is
         * named in full, and not by whatever panel happens to be open on
         * the instance receiving it. */
        panelEdit: (kind, a, b, row, text) =>
            post({ type: 'paneledit', kind, a, b, row, text }),

        /* A stage's parameter, at a transport time.
         *
         * A command, and stamped -- which is where it differs from
         * panelEdit above, whose args are what an instrument is rather than
         * something a composer is heard through. `row' is the param's name
         * and `text' is the part of the line the person touched; every
         * instance completes it against the piece it holds. */
        param: ({ at = -1, chain, stage, row, text }) =>
            post({ type: 'param', at, chain, stage, row, text }),

        /* A gesture on a stage's picture, already in the coordinates the
           composer drew in. Handed the command itself, since every field
           of it is one the module wants. */
        input: ({ at = -1, chain, stage, kind, x, y, w, h, button = 1 }) =>
            post({ type: 'input', at, chain, stage, kind, x, y, w, h,
                   button }),

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
            post({ type: 'midion', note, velocity, frame, channel }),

        midiOff: (note, frame = -1, channel = 0) =>
            post({ type: 'midioff', note, frame, channel }),

        noteOn: (note, velocity = 100, frame = -1, channel = 0) =>
            post({ type: 'on', note, velocity, frame, channel }),

        noteOff: (note, frame = -1, channel = 0) =>
            post({ type: 'off', note, frame, channel }),

        allOff: () => post({ type: 'alloff' }),

        /* To the mirror alone: the composer view's size, its pointer, and
           a request for a frame. Nothing here reaches the worklet, which
           has no canvas and no use for any of it. */
        toMirror: (m) => mirror?.postMessage(m),

        /* A tap on one arg of one node of whatever is loaded on a
           channel. Resolves to { slot, why }: a slot is only good until
           the next load on that channel, which disarms every probe
           pointing at it (thSynth.h).
         *
           To the worklet alone, and deliberately: a probe is a tap on
           what is being rendered, and the mirror renders nothing. */
        probe: (channel, nodeName, arg) => new Promise((resolve) =>
        {
            const id = nextId++;

            waiting.set(id, resolve);

            /* `nodeName' and not `node': the AudioWorkletNode is called
               that in this closure, and a parameter that shadowed it
               would have posted the message to a string. */
            node.port.postMessage({ type: 'probe', id, channel,
                                    node: nodeName, arg });
        }),

        unprobe: (slot) =>
            node.port.postMessage({ type: 'unprobe', slot }),

        /* Resolves once the worklet has handled everything sent so far. */
        flush: () => ask({ type: 'ping' }),
    };
}
