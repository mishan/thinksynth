/*
 * Copyright (C) 2004-2026 The thinksynth authors
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

/* Each module's bytes, by file, fetched once. */
const fetched = new Map();

function wasmBytes (name = 'thinkweb.wasm')
{
    if (!fetched.has(name))
        fetched.set(name, fetch(new URL(name, import.meta.url))
            .then((r) =>
            {
                if (!r.ok)
                    throw new Error(`${name}: ${r.status} ${r.statusText}`);

                return r.arrayBuffer();
            })
            .catch((e) =>
            {
                /* Not kept: Start after a failed fetch fetches again. */
                fetched.delete(name);
                throw e;
            }));

    return fetched.get(name);
}

/* The tape batches the load's recent figures are summed over: 64 is
   2.7 s at 48 kHz. */
const RECENT_BATCHES = 64;

/* A batch's process() costs (worklet.js, postTape) added into a sum. */
function addLoad (into, m)
{
    into.overBudget += m.overBudget;
    into.slowestMs = Math.max(into.slowestMs, m.slowestMs);

    for (let ms = 0; ms < m.took.length; ms++)
    {
        into.took[ms] = (into.took[ms] ?? 0) + m.took[ms];
        into.calls += m.took[ms];
    }
}

/* A sum, and the whole millisecond 99% of its calls took no longer than;
   the last bin is that many or more. */
function withP99 (q)
{
    let ms = 0;

    for (let seen = q.took[0] ?? 0; seen < 0.99 * q.calls;
         seen += q.took[ms])
        ms++;

    return { ...q, took: [...q.took], p99Ms: ms };
}

const noLoad = () => ({ calls: 0, overBudget: 0, slowestMs: 0, took: [] });

/* A window of one quantum by default. The worklet is asked for 128 frames at
   a time and has 128 frames' time to make them, so a longer window does all
   of its work inside one quantum and none in the next: its deadline is the
   quantum's, not its own. */
export async function createSynth (ctx, { windowlen = 128,
                                          onLog = () => {},
                                          onTape = () => {},
                                          onParamEdits = () => {},
                                          onMidi = () => {},
                                          onMidiState = () => {},
                                          onEdited = () => {},
                                          onRecorded = () => {},
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
    const tried = new Map();        /* tag -> resolve, see tried()        */
    const growing = new Map();      /* id -> resolve, see grow()          */
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

    /* The ear worker (ear.js), made the first time either realm asks to
       hear: one that never listens never has it. It needs the module's
       bytes and the sound files, so both are kept for it.

       The worklet and the mirror ask the same questions -- both run the
       piece -- so a question asked while the same one is out, or soon
       after it was answered, is not asked again: every render once. A
       question is its request less the asker's ticket and the texts that
       came with it. If the ear fails, every question is answered failed,
       as one it could not hear: a stage then goes on without it. */
    let ear = null;
    let earStart = null;
    let earDead = false;
    let earId = 0;
    const sounds = new Map();
    const asking = new Map();       /* id -> { key, waiters }            */
    const outFor = new Map();       /* key -> id                         */
    const answers = new Map();      /* key -> answer, the latest few     */
    const ANSWERS_KEPT = 256;
    const reply = (waiters, r) =>
    {
        for (const w of waiters)
            (w.to === 'mirror' ? mirror : node.port)?.postMessage(
                { type: 'heard', ticket: w.ticket, ok: r.ok,
                  distance: r.distance, tuned: r.tuned });
    };
    const failEar = (text) =>
    {
        earDead = true;
        onLog(text);

        for (const { waiters } of asking.values())
            reply(waiters, { ok: false, distance: 0, tuned: '' });

        asking.clear();
        outFor.clear();
    };
    const hear = (to, ask) =>
    {
        const a = JSON.parse(ask);
        const waiter = { to, ticket: a.ticket };

        if (ear === null && earStart !== null && !earDead)
        {
            ear = new Worker(new URL('ear.js', import.meta.url),
                             { type: 'module' });
            ear.onmessage = (e) =>
            {
                const m = e.data;

                if (m.type === 'heard')
                {
                    const out = asking.get(m.id);

                    if (out === undefined)
                        return;

                    asking.delete(m.id);
                    outFor.delete(out.key);
                    answers.set(out.key, m);

                    if (answers.size > ANSWERS_KEPT)
                        answers.delete(answers.keys().next().value);

                    reply(out.waiters, m);
                }
                else if (m.type === 'failed')
                    failEar(m.text);
                else if (m.type === 'log')
                    onLog(m.text);
            };
            ear.onerror = (e) => failEar(`ear: ${e.message ?? 'failed'}`);
            ear.postMessage(earStart());

            for (const [name, bytes] of sounds)
                ear.postMessage({ type: 'sample', name, bytes });
        }

        if (ear === null || earDead)
        {
            reply([waiter], { ok: false, distance: 0, tuned: '' });
            return;
        }

        if (Object.keys(a.texts).length > 0)
            ear.postMessage({ type: 'texts', texts: a.texts });

        delete a.ticket;
        delete a.texts;

        const key = JSON.stringify(a);
        const known = answers.get(key);

        if (known !== undefined)
        {
            reply([waiter], known);
            return;
        }

        const out = outFor.get(key);

        if (out !== undefined)
        {
            asking.get(out).waiters.push(waiter);
            return;
        }

        const id = earId++;

        asking.set(id, { key, waiters: [waiter] });
        outFor.set(key, id);
        ear.postMessage({ type: 'hear', id, ask: a });
    };

    /* A growth the mirror made for the worklet goes straight back to it,
       and what it asks to hear to the ear; everything else the mirror
       says is the caller's. */
    if (mirror !== null)
        mirror.onmessage = (e) =>
        {
            if (e.data.type === 'grown')
                node.port.postMessage(e.data);
            else if (e.data.type === 'grownnow')
            {
                growing.get(e.data.id)?.(e.data);
                growing.delete(e.data.id);
            }
            else if (e.data.type === 'hear')
                hear('mirror', e.data.ask);
            else
                onMirror(e.data);
        };

    /* What the worklet's process() calls have cost, summed since the
       start: a batch is 16 calls, too few to have a p99 of its own. And
       the last RECENT_BATCHES batches, kept apart, because the first
       quantum of a run builds the graphs and is the slowest by far, and
       a figure since the start goes on showing it for good. */
    const quanta = noLoad();
    const recent = [];
    let batches = 0;

    /* One message, both ports. See the top of this file. */
    /* Collecting, inside batch() below. */
    let batching = null;

    const post = (m) =>
    {
        if (batching !== null)
        {
            batching.push(m);
            return;
        }

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
            /* A growth step for the mirror to make (thinkweb.cpp,
               tw_grow_mode). With no mirror, nothing grows. */
            case 'grow':
                mirror?.postMessage({ type: 'growask', ask: m.ask });
                break;
            case 'hear':
                hear('worklet', m.ask);
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
            case 'tried':
                for (const { tag, why } of m.tried)
                {
                    tried.get(tag)?.(why);
                    tried.delete(tag);
                }
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
            case 'genparam':
            case 'genseed':
            case 'chains':
            case 'genaddstage':
            case 'genops':
            case 'genmove':
            case 'gensection':
            case 'geninstrument':
            case 'genfreeze':
            case 'genknob':
            case 'geneffect':
            case 'patchdefault':
            case 'patchdefaults':
            case 'dsps':
            case 'gens':
            case 'growntext':
            case 'geninline':
            case 'evolvespec':
                waiting.get(m.id)?.(m);
                waiting.delete(m.id);
                break;
            case 'paramedits':
                onParamEdits(m);
                break;
            /* MIDI out: the stamped messages for the page to send
               (midiout.js), and where each MIDI instrument plays after the
               ports or a route changed. */
            case 'midi':
                onMidi(m.msgs);
                break;

            case 'midistate':
                onMidiState(m.instruments);
                break;

            case 'edited':
                onEdited(m);
                break;
            case 'recorded':
                onRecorded(m);
                break;
            case 'tape':
                /* And the mirror is told how far this has got: it steps
                   to there, which is tw_render without the render. So its
                   picture is one batch behind the ear, which is about the
                   desktop's 50 ms draw timer. The keys this one played
                   go with it, for the roll the mirror draws. */
                mirror?.postMessage({ type: 'step', frame: m.frame,
                                      epoch: m.epoch, keys: m.keys });
                onTape(m);

                /* After the tape, and only when the batch carries them: a
                   worklet.js from another build may post none, and a load
                   figure is not worth losing the tape over. */
                if (m.took === undefined)
                    break;

                addLoad(quanta, m);
                quanta.coarseClock = m.coarseClock;
                recent[batches++ % RECENT_BATCHES] =
                    { overBudget: m.overBudget, slowestMs: m.slowestMs,
                      took: m.took };
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

    earStart = () =>
    {
        const forEar = bytes.slice(0);

        return { type: 'start', bytes: forEar, windowlen: info.windowlen,
                 sampleRate: info.sampleRate };
    };

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

        /* The sums above, the recent one as `recent'. */
        quanta: () =>
        {
            const last = noLoad();

            for (const b of recent)
                addLoad(last, b);

            return { ...withP99(quanta), recent: withP99(last) };
        },

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
        sample: (name, bytes) =>
        {
            /* A target may be this file, heard before it was written over. */
            answers.clear();
            sounds.set(name, bytes);
            ear?.postMessage({ type: 'sample', name, bytes });
            post({ type: 'sample', name, bytes });
        },

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
        loadPiece: (text, seed = -1) =>
        {
            /* A piece that pins no seed composes from one drawn here, once,
               and the same one is handed to both instances: left to draw
               their own, the worklet would play one piece and the mirror
               draw another on the canvas and the roll. A piece that pins
               its own is not moved by this. */
            if (!(seed >= 0))
                seed = Math.floor(Math.random() * 0x100000000);

            return ask({ type: 'piece', text, seed });
        },

        /* 'start', 'stop', 'rewind' or 'tempo', the last with a value in
           beats per minute, at a frame; -1 is the next window. 'start'
           resumes from where the transport is. */
        transport: (op, value = 0, frame = -1) =>
            post({ type: 'transport', op, value, frame }),

        /* From the top, with transport zero at `frame' exactly: what a
           room's Play is, on every peer, at the frame its origin falls
           on. `catchUp' for a peer joining a room already playing: a
           frame gone by is kept, and the transport stepped up to the
           output silently from there (thinkweb.cpp, tw_begin). */
        begin: (frame, from = 0, catchUp = false) =>
            post({ type: 'begin', frame, from, catchUp }),

        /* Everything `fn' posts, as one message: applied together, between
           two renders, in the order posted (engine.js). A late joiner's
           begin and the commands it catches up through are the reason. */
        batch: (fn) =>
        {
            batching = [];

            try
            {
                fn();
            }
            finally
            {
                const messages = batching;

                batching = null;
                post({ type: 'batch', messages });
            }
        },

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

        /* `text' with one stage's param set, as a `paramedits' edit names
           it, or "" if the edit was refused. Resolves to `{ text }'. For a
           room's document, which is not the piece the worklet holds. */
        genSetParam: (text, { chainName, docStage, param, valueText }) =>
            ask({ type: 'genparam', text, chain: chainName, stage: docStage,
                  param, valueText }),

        /* A stage added behind chain `chainName''s others in `text',
           `params' as { name: valueText }. Resolves to { text }, "" when
           the writer refused. */
        genAddStage: (text, chainName, stage, category, plugin, params) =>
            ask({ type: 'genaddstage', text, chain: chainName, stage,
                  category, plugin,
                  params: Object.entries(params)
                      .map(([k, v]) => `${k}=${v}`).join('\n') }),

        /* Several edits made to `text' as one: each an array of fields
           (thinkweb.cpp, tw_gen_ops). Resolves to { text, why }, text ""
           when one was refused. */
        genOps: (text, ops) =>
            ask({ type: 'genops', text,
                  ops: ops.map((f) => f.join('\t')).join('\n') }),

        /* The piece's chains as its text says them now, and its sections
           as they play. Resolves to { chains, sections }. */
        pieceChains: () => ask({ type: 'chains' }),

        /* Chain `chainName' given its own seed in `text', or the piece's
           back when `seed' is null. Resolves to { text }, "" when the
           writer refused. */
        genSetChainSeed: (text, chainName, seed) =>
            ask({ type: 'genseed', text, chain: chainName,
                  pin: seed !== null, seed: seed ?? 0 }),

        /* One stage of a chain moved to another place in it, in `text';
           stages in the document's numbering. Resolves to { text }, ""
           when the writer refused. */
        genMoveStage: (text, chainName, from, to) =>
            ask({ type: 'genmove', text, chain: chainName, from, to }),

        /* Instrument `name' on graph `dsp' in `text', from that graph's
           own values. Resolves to { text, why }, "" when refused. */
        genSetInstrument: (text, name, dsp) =>
            ask({ type: 'geninstrument', text, name, dsp }),

        /* The piece's effect on the mix in `text', or none for "".
           Resolves to { text }, "" when the writer refused. */
        genSetEffect: (text, dsp) => ask({ type: 'geneffect', text, dsp }),

        /* A chain frozen into `text' from what the worklet heard it play
           in the last `bars' bars. Resolves to { text, why }. */
        genFreeze: (text, chain, bars) =>
            ask({ type: 'genfreeze', text, chain, bars }),

        /* Instrument `instrument''s graph written into `text' as its
           `dsp { body }'. Resolves to { text, why }, text "" refused. */
        genSetInline: (text, instrument, body) =>
            ask({ type: 'geninline', text, instrument, body }),

        /* Channel `channel' back to `depth' edits on its declared graph,
           at a transport time or -1 for the next window, in both realms. */
        growBack: ({ at = -1, channel, depth, rev }) =>
            post({ type: 'growback', at, channel, depth, rev }),

        /* Up to `want' edits of `text', the graph `base' was before any,
           of the kinds `kinds' names, picked by `choice' (NodeMutate::grow):
           made in the mirror, which composes and has time for it. Resolves
           to { grown, what, why }, `grown' and `what' as lists. */
        grow: (text, base, choice, kinds, want = 1) =>
            new Promise((resolve) =>
            {
                const split = (s) => (s === '' ? [] : s.split('\x1e'));

                if (mirror === null)
                {
                    resolve({ grown: [], what: [], why: 'nothing to grow in' });
                    return;
                }

                const id = nextId++;

                growing.set(id, (m) => resolve({ grown: split(m.grown),
                                                 what: split(m.what),
                                                 why: m.why }));
                mirror.postMessage({ type: 'grownow', id, text, base, choice,
                                     kinds, want });
            }),

        /* Where a search of `instrument' toward `toward' -- an instrument,
           or '' for a recording -- starts, as the worklet plays it
           (thinkweb.cpp, tw_evolve_spec). */
        evolveSpec: (instrument, toward) =>
            new Promise((resolve) =>
            {
                const id = nextId++;

                waiting.set(id, resolve);
                node.port.postMessage({ type: 'evolvespec', id, instrument,
                                        toward });
            }),

        /* Evolve toward on `workers' of the machine's cores (evolve.js).
           Where the browser can suspend a module on a promise (JSPI), one
           search whose generations `workers' listeners score; elsewhere
           `workers' searches on seeds from `seed', of which the caller
           keeps the best. Each says what it finds to onEvent(search,
           message) and ends itself when it is done. Resolves to
           { cancel, searches }: cancel() ends whatever is still going, and
           `searches' is how many will say they are done. */
        evolve: async (spec, { seed, workers, sound }, onEvent) =>
        {
            const started = [];
            const cancel = () => started.forEach((w) => w.terminate());
            const spawn = (message, transfer, onMessage, onError) =>
            {
                const w = new Worker(new URL('evolve.js', import.meta.url),
                                     { type: 'module' });

                w.onmessage = onMessage;
                w.onerror = (e) =>
                {
                    w.terminate();
                    onError(e.message ?? 'the search failed');
                };
                /* The sound files too: an instrument that plays one --
                   a kit, a pack -- sounds of nothing without it. */
                w.postMessage({ windowlen: info.windowlen,
                                sampleRate: info.sampleRate, spec, sound,
                                sounds: [...sounds], ...message }, transfer);
                started.push(w);
            };

            const shared = typeof WebAssembly.Suspending === 'function'
                ? await wasmBytes('thinkevolve.wasm').catch(() => null)
                : null;

            if (shared !== null)
            {
                let ended = false;
                const done = (m) =>
                {
                    if (ended)
                        return;

                    ended = true;
                    cancel();
                    onEvent(0, m);
                };
                const failed = (why) =>
                    done({ type: 'done', ok: false, why });
                const ears = [];

                for (let i = 0; i < workers; i++)
                {
                    const { port1, port2 } = new MessageChannel();
                    const copy = bytes.slice(0);

                    spawn({ bytes: copy, port: port2 }, [copy, port2],
                          () => {}, failed);
                    ears.push(port1);
                }

                const copy = shared.slice(0);

                spawn({ bytes: copy, seed, ears }, [copy, ...ears],
                      (e) => (e.data.type === 'done' ? done(e.data)
                                                     : onEvent(0, e.data)),
                      failed);

                return { cancel, searches: 1 };
            }

            for (let i = 0; i < workers; i++)
            {
                const copy = bytes.slice(0);

                spawn({ bytes: copy, seed: seed + i }, [copy], (e) =>
                {
                    if (e.data.type === 'done')
                        started[i].terminate();

                    onEvent(i, e.data);
                }, (why) => onEvent(i, { type: 'done', ok: false, why }));
            }

            return { cancel, searches: workers };
        },

        /* A search's graph on `instrument''s channel in place of `over',
           at a transport time or -1 for the next window, in both realms
           (thinkweb.cpp, tw_try_graph); `graph' '' takes `over' off.
           What it came to goes to tried(tag). */
        tryGraph: ({ at = -1, instrument, graph, over, strict, tag }) =>
            post({ type: 'trygraph', at, instrument, graph, over, strict,
                   tag }),

        /* What the tryGraph tagged `tag' came to once the worklet applied
           it: '' when it is on its channel, or why not. A load can drop a
           command still waiting for its time, so this does not wait for
           ever. */
        tried: (tag, timeoutMs = 5000) => new Promise((resolve) =>
        {
            const timer = setTimeout(() =>
            {
                tried.delete(tag);
                resolve('the synth did not say what became of it');
            }, timeoutMs);

            tried.set(tag, (why) =>
            {
                clearTimeout(timer);
                resolve(why);
            });
        }),

        /* One chain's level in one section, in `text'. Resolves to
           { text }, "" when the writer refused. */
        genSetSection: (text, sectionName, chainName, level) =>
            ask({ type: 'gensection', text, section: sectionName,
                  chain: chainName, level }),

        /* How fast the clock runs, as a multiple of real time, at a
           transport time or -1 for the next window. Everything moves with
           it, which is what makes it the control a piece written in
           seconds has where the tempo is the one it has not. */
        speed: (value, at = -1) => post({ type: 'speed', value, at }),

        /* `knob' is the index loadPiece reported the knob under, or its
           name, which is looked up when the command applies; `at' a
           transport time, or -1 for the next window. */
        knob: (knob, value, at = -1) => post({ type: 'knob', knob, value, at }),

        /* The same knob's value written into the piece, at the end of a
           drag; it comes back with the param edits. */
        knobWrite: ({ at = -1, knob, value, tag = '', rev }) =>
            post({ type: 'knobwrite', at, knob, value, tag, rev }),

        /* A knob's value in `text', for a room's document. Resolves to
           { text }, "" when refused. */
        genSetKnob: (text, name, value) =>
            ask({ type: 'genknob', text, name, value }),

        /* A new text for the piece at transport time `at', and the other
           files the edit changed, as { name: text }. What it keeps of the
           piece playing is thcGenDiff's rule; `onEdited' hears what the
           piece is afterwards. */
        edit: (at, text, files = {}, tie = 0) =>
            post({ type: 'edit', at, text, files, tie }),

        /* A key at a transport time: pressed when `on', into the piece or
           onto the channel as the piece stands when it applies, dropped in
           the second case when `heard' says its player played it live, and
           ordered among commands stamped for the same time by `tie'
           (commands.js, tieOf). What a quantised or play-ahead seat's key
           is. */
        noteAt: (at, channel, note, velocity, on, heard, tie) =>
            post({ type: 'noteat', at, channel, note, velocity, on, heard,
                   tie }),

        /* Instrument `name' onto graph `dsp' at transport time `at', in the
           piece as it stands then; `onEdited' hears it as an edit. */
        pick: (at, name, dsp, tie) =>
            post({ type: 'pick', at, name, dsp, tie }),

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

        /* The graph growth has put on `instrument''s channel in the
           worklet, or '' while it plays what the piece declares. */
        grownText: (instrument) =>
            new Promise((resolve) =>
            {
                const id = nextId++;

                waiting.set(id, (m) => resolve(m.text));
                node.port.postMessage({ type: 'growntext', id, instrument });
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

        /* A stage's parameter, at a transport time.
         *
         * A command, and stamped -- which is where it differs from
         * panelEdit above, whose args are what an instrument is rather than
         * something a composer is heard through. `row' is the param's name
         * and `text' is the part of the line the person touched; every
         * instance completes it against the piece it holds. */
        param: ({ at = -1, chain, stage, chainName, stageName, row, text,
                  rev, passing = false }) =>
            post({ type: 'param', at, chain, stage, chainName, stageName, row,
                   text, rev, passing }),

        /* A chain's live mute or solo, at a transport time or -1 for the
           next window. Not written into the piece. */
        mute: ({ at = -1, chain, on, rev }) =>
            post({ type: 'mute', at, chain, on, rev }),

        /* The output recorded from now, or no longer: a second at a time
           to onRecorded, the last with `done'. The worklet's alone: the
           mirror renders nothing to record. */
        record: (on) => node.port.postMessage({ type: 'record', on }),

        solo: ({ at = -1, chain, on, rev }) =>
            post({ type: 'solo', at, chain, on, rev }),

        /* A chain's level in one section, at a transport time or -1 for
           the next window. Written into the piece; the splice comes back
           with the param edits. */
        section: ({ at = -1, section, chain, level, rev }) =>
            post({ type: 'section', at, section, chain, level, rev }),

        /* A gesture on a stage's picture, already in the coordinates the
           composer drew in. Handed the command itself, since every field
           of it is one the module wants -- the stage's names too, which
           it finds the stage by after an edit has moved it. */
        input: ({ at = -1, chain, stage, chainName, stageName, kind, x, y, w,
                  h, button = 1, tag = '', rev }) =>
            post({ type: 'input', at, chain, stage, chainName, stageName, kind,
                   x, y, w, h, button, tag, rev }),

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
        /* The page's MIDI output ports, by name, or none (enabled
           false), and which list this is: the worklet stamps every
           message with it, so one indexing an older list is dropped.
           Each MIDI instrument whose place changed is applied again, onto
           a device or onto its dsp. And this browser's route for one pattern. To
           the worklet alone: the mirror plays the dsp and composes the
           same. */
        midiPorts: (names, enabled = true, generation = 0) =>
            node.port.postMessage({ type: 'midiports', names, enabled,
                                    generation }),

        midiRoute: (pattern, to) =>
            node.port.postMessage({ type: 'midiroute', pattern, to }),

        /* Whether the page has outputs for MIDI clock: the worklet sends
           ticks only then. */
        midiClock: (on) =>
            node.port.postMessage({ type: 'midiclock', on }),

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
