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
    /* A browser offers the worklet only in a secure context: https, or
       localhost. A page opened as http:// from another machine -- a phone
       pointed at a laptop's serve.mjs -- has an AudioContext and no
       audioWorklet, and said so as "reading 'addModule'". */
    if (ctx.audioWorklet === undefined)
        throw new Error('the browser allows audio here only over https or ' +
                        'on localhost');

    const [bytes] = await Promise.all([
        wasmBytes(),
        ctx.audioWorklet.addModule(new URL('worklet.js', import.meta.url)),
    ]);

    /* One input, for a live signal to arrive on: a microphone, a line in,
       anything a MediaStream carries. Declared whether or not anything is ever
       connected to it, because a worklet's input count is fixed at
       construction and asking for the mic is a click that happens later. An
       input nothing is connected to costs the worklet an empty array per
       quantum and the graph nothing -- see mic.js, which is what connects one,
       and the worklet's feedInput, which is what reads it. */
    const node = new AudioWorkletNode(ctx, 'thinksynth', {
        numberOfInputs: 1,
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
            case 'patched':
            case 'probed':
                waiting.get(m.id)?.(m);
                waiting.delete(m.id);
                break;
            case 'piece':
            case 'panel':
            case 'panelvalues':
            case 'patchstate':
            case 'patchcompose':
            case 'settempo':
            case 'patchdefault':
            case 'patchdefaults':
            case 'dsps':
            case 'gens':
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

        /* One of the shipped pieces, under the name the menu lists it by.
           Nothing plays from these -- a piece is played by handing its text
           to loadPiece -- they are here so that the module can read their
           headers, which is the same reason it is handed the graphs. */
        piecefile: (name, text) => post({ type: 'piecefile', name, text }),

        /* And what those pieces say about themselves: the title, the
           description and the category each declares, grouped the way the
           Composer's Open groups them (src/GenCatalog.h). */
        gens: () => ask({ type: 'gens' }),

        /* What those graphs say about themselves -- the title, the
           description and whether each is an effect -- grouped the way a menu
           wants them. Asked of the worklet because that is where the files
           are: it has been handed every one of them, and the module reads
           their headers with the class the desktop's chooser uses
           (src/DspCatalog.h). Ask it after the instruments are over. */
        dsps: () => ask({ type: 'dsps' }),

        /* And a wav under the name an osc::sample node's `file' will ask
           for, which is `samples/kick909.wav' -- the index.json entry,
           since that is the path and not the basename. Bytes, because it
           is not text. */
        sample: (name, bytes) => post({ type: 'sample', name, bytes }),

        /* A whole .patch, as text: the module reads it and puts it on the
           channel, in the order the format requires. Resolves to
           `{ ok, why, json }' -- the document it read, so the page can say
           what it put on without reading the file a second time.

           `name' is what to call the slot -- the name the page fetched it
           by, which the bytes do not carry and which a Save would offer
           back.

           The .dsp it names is not sent: the page has already handed every
           shipped graph to instrument() above, which is where a patch's
           `dsp' line is resolved from. */
        patch: (channel, text, name = '') =>
            ask({ type: 'patch', channel, text, name }),

        /* What is on a channel now: the document, the name it was given,
           and whether it has been edited since. Resolves to `{ json }',
           empty for a channel nothing has been put on. Asked of the
           worklet alone -- it is a question, and the instance that sounds
           is the one whose answer counts. */
        patchState: (channel) => ask({ type: 'patchstate', channel }),

        /* The bytes a Save would write for a channel: the slot's graph,
           effect, side and info, and the values the channel holds now.
           Resolves to `{ text }', empty for a channel nothing is on. The
           same bytes the desktop writes, which is what makes a patch saved
           in a browser one the application opens. */
        patchCompose: (channel, stamp) =>
            ask({ type: 'patchcompose', channel, stamp }),

        /* And that they were kept, under this name: the other half of the
           dirty flag the channel row draws. Told rather than asked -- a
           page that composed a patch and then thought better of it has not
           saved anything, so this is a separate thing to say. */
        patchSaved: (channel, name) =>
            post({ type: 'patchsaved', channel, name }),

        /* What belongs on a channel nothing has aimed. Resolves to
           `{ name }', empty for a channel with no answer; patchDefaults()
           is the distinct list, for fetching them all before the first
           load. The rule is the module's -- see src/PatchSet.h. */
        patchDefault: (channel) => ask({ type: 'patchdefault', channel }),
        patchDefaults: () => ask({ type: 'patchdefaults' }),

        /* One chanarg of whatever is loaded on a channel. The other half of
           load() for anything that drives the two by hand; a .patch goes
           through patch() above, which does them in the order
           src/PatchApply.h sets out. A name the tree does not declare is
           ignored and said once in the log. */
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

        /* The piece's text with its `tempo' set to `bpm', or "" if the
           edit was refused. Resolves to `{ text }'.
         *
           The other half of a tempo change: transportAt('tempo', ...)
           moves what is playing, this moves what a reload would come back
           at. Two calls because the page keeps the document in a box
           somebody may have typed into, and only the page knows whether
           what is in it is still what was loaded. */
        pieceSetTempo: (bpm) => ask({ type: 'settempo', bpm }),

        /* How fast the clock runs, as a multiple of real time, at a
           transport time or -1 for the next window. Everything moves with
           it, which is what makes it the control a piece written in
           seconds has where the tempo is the one it has not. */
        speed: (value, at = -1) => post({ type: 'speed', value, at }),

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

        /* One numeric param of one composer stage, at a transport time or
           -1 for the next window. The stage is named the way a gesture
           names one: chain and stage index, the same on every peer holding
           the same document. */
        stageParam: ({ at = -1, chain, stage, param, value }) =>
            post({ type: 'stageparam', at, chain, stage, param, value }),

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
