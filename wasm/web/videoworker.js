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
 */

/*
 * videoworker.js -- a video synth played live, off the page's thread and
 * off the audio's.
 *
 * Its own realm and its own instance of the module (twvideo.cpp), because
 * a picture is a million samples a second and the worklet's budget is the
 * audio's. The worklet sends what it played and what the sound says in
 * batches every few quanta, on a port of its own; this draws as far as the audio has been
 * generated and hands each finished frame to the page, which shows it
 * when the sound it goes with is heard.
 *
 * Messages only queue. Drawing happens in slices of a few milliseconds
 * between them, so the newest batch is always known before the next slice
 * and a picture that cannot keep up skips to the frame that is due
 * instead of drawing every one it has fallen behind on.
 *
 * Every frame number on the way in is the audio context's (its time times
 * its rate); the video's own frames count from the first batch.
 *
 * It draws with as few workers as keep up (videoscale.js): itself, and
 * helpers (videopart.js) once it falls behind, one a core past the page's
 * and the audio's at most and no more than the channels. Each draws some
 * of the channels as float planes, sent every batch stamped from this
 * worker's start; this sums each frame's parts in order, turns them to
 * RGBA and decides every skip, the same for all. A channel moves between
 * workers at a moment: the one it leaves keeps the notes struck before,
 * release and all, until they have faded, and the one it goes to takes
 * those struck from then on (tv_window), so a move cuts nothing. A helper
 * let go leaves once its channels have faded there, or after FORCE_MS by
 * opening every worker again. A picture with an effect is drawn here
 * alone.
 */

import createThinkWeb from './thinkweb.js';
import { Scaler } from './videoscale.js';

/* twvideo.cpp's note record. */
const RECORD = 11;

/* How long one slice of drawing may take before messages are looked at. */
const SLICE_MS = 4;


/* A frame is wanted for the screen's next refresh, not this moment. */
const AHEAD_MS = 1000 / 60;

/* Helpers at most; a helper quiet STALL_MS (or four of its frames) longer
   than the others is given up on, as is one not started in START_MS. */
const MOST_HELPERS = 8, STALL_MS = 250, START_MS = 2000;

/* Frames a worker has out with this one at most: videopart.js's MOST, and
   this worker's own. A worker draws only with two of them free. */
const PART_BUFFERS = 4;

/* Frames ahead of the next composed that a helper joins at. */
const JOIN_AHEAD = 3;

/* How long a helper let go may take for its channels to fade there before
   every worker is opened again without it. */
const FORCE_MS = 10000;

let M = null;
const early = [];

let raster = null;      /* { width, height, fps, lines, rate } */
let patch = null, effect = null;
let following = new Set();
let knobNames = [];
let origin = null;      /* the audio frame video frame 0 starts at */
let clock = null;       /* { offset, timeOrigin, rate } from the page */
/* Frames out with the page at once, as many as the page says it holds
   while their sound reaches the speakers; with none back, a frame is not
   sent. */
let most = 0, made = 0;
const pool = [];
let pending = false;
/* Time spent drawing here since `since'; skips since then. */
let spent = 0, since = performance.now(), skipped = 0;
let settling = false;   /* catching up to now after an open */
let knobs = [];         /* their values as last set */
let inputs = new Float32Array(4);   /* the sound's, as last heard */
let worklet = null;     /* the worklet's port */

let module = null, epoch = 0, override = null, cap = MOST_HELPERS + 1;
let lastOpen = null, next = 0, lastAt = 0, narrowSaid = false, patchSaid = false;
let scaler = null, told = 0;
/* The page's own sets, put back when this worker opens again of itself,
   which the page doesn't know to resend. */
const pageSets = new Map();
/* frame -> worker -> its planes for that frame. */
const parts = new Map();

/* The workers drawing: this one first, then its helpers. Each has its
   share of the channels, whose notes it strikes, those it has handed on
   and still draws the fading voices of, when it began to count toward a
   frame (joinAt) and stops (leaveAt), and what it last said: how busy,
   which channels it draws are silent there. */
let self = null;
let workers = [];
const ownPool = [];
let ownMade = 0;

const post = (m, transfer) => postMessage(m, transfer ?? []);

/* The module the page compiled, instantiated here. */
async function start (m)
{
    module = m.module;
    try
    {
        M = await createThinkWeb({
            instantiateWasm: (imports, done) =>
            {
                WebAssembly.instantiate(module, imports)
                    .then((instance) => done(instance, module))
                    .catch((e) => post({ type: 'failed',
                        text: `the video's module would not start: ${e}` }));

                return {};
            },
            print: () => {},
            printErr: () => {},
        });
    }
    catch (e)
    {
        post({ type: 'failed', text: `the video's module: ${e}` });
        return;
    }

    for (const m of early.splice(0))
        receive(m);
}

function worker (fields)
{
    return { share: new Set(), fading: new Set(), busy: 0, last: performance.now(),
             gap: 1000 / raster.fps, ready: false, joinAt: Infinity, leaveAt: Infinity,
             retiring: 0, silent: new Set(), started: performance.now(), ...fields };
}

const helpers = () => workers.filter((w) => w !== self);
const active = () => workers.filter((w) => w.ready && w.joinAt !== Infinity && !w.retiring);

/* The most workers: the cores past the page's and the audio's, the
   channels, and how many this machine has shown it can run. */
function limit ()
{
    if (effect !== null)
        return 1;

    const cores = (navigator.hardwareConcurrency ?? 2) - 1;

    return Math.max(1, Math.min(cores, following.size, MOST_HELPERS + 1, cap));
}

function followHere (channel)
{
    const why = M.ccall('tv_follow', 'string', ['number', 'string', 'string'],
                        [channel, patch.name, patch.text]);

    if (why !== '' && !patchSaid)
    {
        patchSaid = true;
        post({ type: 'failed', text: why });
    }
}

const ALWAYS = 1e300;

/* What `w' is told about `channel': to take its notes struck from `at'
   on, to keep only those before `at', or to draw it no more. */
function take (w, channel, at)
{
    if (w === self)
    {
        followHere(channel);
        M._tv_window(channel, at, ALWAYS);
    }
    else
        w.worker.postMessage({ type: 'take', channel, at });
}

function give (w, channel, at)
{
    if (w === self)
        M._tv_window(channel, -ALWAYS, at);
    else
        w.worker.postMessage({ type: 'give', channel, at });
}

function drop (w, channel)
{
    if (w === self)
        M._tv_unfollow(channel);
    else
        w.worker.postMessage({ type: 'drop', channel });
}

/* `channel' to `w' from the moment the audio has reached: its notes from
   then on struck there, and those before left fading where they were. */
function place (channel, w)
{
    const from = workers.find((o) => o.share.has(channel));

    if (from === w)
        return;
    if (from !== undefined)
    {
        from.share.delete(channel);
        from.fading.add(channel);
        from.silent.delete(channel);
        give(from, channel, lastAt);
    }
    w.share.add(channel);
    take(w, channel, from === undefined ? -ALWAYS : lastAt);
}

/* The joined worker with the fewest channels, other than `but', and not
   one `channel' still fades on, which would need two windows; or none. */
const fewest = (channel = null, but = null) =>
    active().filter((w) => w !== but && !w.fading.has(channel))
            .reduce((a, b) => (a === undefined || b.share.size < a.share.size ? b : a),
                    undefined);

/* `again': opened by this worker rather than the page; `count' workers
   to deal the channels among at once, since every one starts afresh. */
function open (m, again = false, count = 1)
{
    lastOpen = m;
    override = m.helpers ?? override;
    lastAt = 0;
    if (!again)
    {
        pageSets.clear();
        patchSaid = false;
        cap = MOST_HELPERS + 1;
    }
    for (const w of helpers())
        w.worker.terminate();
    parts.clear();
    next = 0;
    epoch++;
    raster = { width: m.width, height: m.height, fps: m.fps, lines: m.lines,
               rate: m.rate };
    most = m.pool;
    settling = true;
    knobs = [];
    patch = m.patch;
    effect = m.fx ?? null;
    origin = null;
    pool.length = 0;
    made = 0;
    ownPool.length = 0;
    ownMade = 0;

    const why = M.ccall('tv_open', 'string',
                        ['number', 'number', 'number', 'number', 'number'],
                        [m.width, m.height, m.fps, m.lines, m.rate]);

    if (why !== '')
    {
        raster = null;
        post({ type: 'failed', text: why });
        return;
    }
    M._tv_planes(1);

    if (effect !== null)
    {
        const fxWhy = M.ccall('tv_effect', 'string', ['string', 'string'],
                              [effect.name, effect.text]);

        if (fxWhy !== '')
            post({ type: 'failed', text: fxWhy });
    }

    self = worker({ ready: true, joinAt: 0 });
    workers = [self];
    scaler = new Scaler({ limit: limit(),
                          fixed: override === null ? null : override + 1 });

    const want = override === null ? count : override + 1;

    for (let k = 1; k < Math.min(want, limit()); k++)
        addHelper(0);
    for (const ch of following)
        place(ch, workers.reduce((a, b) => (b.share.size < a.share.size ? b : a)));
    say();
    for (const [name, value] of pageSets)
        set(name, value);

    /* Opened with nothing held: what is down already, after this. */
    worklet?.postMessage({ type: 'resend' });
}

/* Every worker opened again, `count' of them. */
function reopen (count)
{
    const list = following;

    following = new Set(list);
    open(lastOpen, true, count);
}

/* A helper, counting toward frames from `joinAt' once it has started, or
   from a few frames on from when it starts. */
function addHelper (joinAt = null)
{
    const w = worker({ worker: new Worker(new URL('videopart.js', import.meta.url),
                                          { type: 'module' }),
                       joinFrom: joinAt });

    w.worker.onmessage = (e) => fromHelper(w, e.data);
    w.worker.onerror = () => lose(w);
    workers.push(w);
    w.worker.postMessage({ type: 'start', module });
    w.worker.postMessage({ type: 'open', width: raster.width, height: raster.height,
                           fps: raster.fps, lines: raster.lines, rate: raster.rate,
                           patch, share: [], epoch });
    /* What it would have been sent already: the sound, the knobs, the
       page's sets. */
    w.worker.postMessage({ type: 'batch', at: lastAt, notes: new Float64Array(0),
                           inputs, knobs: knobNames.map((n, k) => [`knob_${n}`, knobs[k]])
                                                   .filter(([, v]) => v !== undefined) });
    for (const [name, value] of pageSets)
        w.worker.postMessage({ type: 'set', name, value, at: lastAt });
    return w;
}

/* A helper given up on: every worker opened again, one fewer. */
function lose (w)
{
    if (!workers.includes(w))
        return;
    cap = Math.max(1, workers.length - 1);
    reopen(cap);
}

function say ()
{
    const n = active().length;

    if (n !== told)
        post({ type: 'workers', count: n });
    told = n;
}

function fromHelper (w, m)
{
    if (!workers.includes(w))
        return;
    switch (m.type)
    {
    case 'ready':
        w.ready = true;
        w.last = performance.now();
        /* It draws from where the others are: a few frames on, or, opened
           with them, now. */
        w.joinAt = Math.max(w.joinFrom ?? next + JOIN_AHEAD, next);
        if (w.joinAt > 0)
            w.worker.postMessage({ type: 'skip', frame: w.joinAt });
        say();
        soon();
        break;
    case 'part':
        if (m.epoch !== epoch)
            return;
        /* Its own pace: how long it takes between parts, smoothed. */
        w.gap += (performance.now() - w.last - w.gap) / 8;
        w.last = performance.now();
        if (m.frame < Math.max(next, w.joinAt) || m.frame >= w.leaveAt)
        {
            giveBack(w, m.buffer);
            return;
        }
        if (!parts.has(m.frame))
            parts.set(m.frame, new Map());
        parts.get(m.frame).set(w, m.buffer);
        soon();
        break;
    case 'busy':
        w.busy = m.fraction;
        w.silent = new Set(m.silent);
        break;
    case 'narrow':
        if (!narrowSaid)
            post({ type: 'narrow', text: m.text });
        narrowSaid = true;
        break;
    case 'failed':
        /* A patch that won't load is the patch's to say, once; a module
           that won't start is this helper's. */
        if (m.patch)
        {
            if (!patchSaid)
                post({ type: 'failed', text: m.text });
            patchSaid = true;
        }
        else
            lose(w);
        break;
    }
}

function channels (list)
{
    for (const ch of [...following])
        if (!list.includes(ch))
        {
            const w = workers.find((o) => o.share.has(ch));

            following.delete(ch);
            for (const o of workers)
                if (o.share.has(ch) || o.fading.has(ch))
                {
                    o.share.delete(ch);
                    o.fading.delete(ch);
                    drop(o, ch);
                }
        }
    for (const ch of list)
        if (!following.has(ch))
        {
            following.add(ch);
            if (raster !== null)
                place(ch, fewest(ch) ?? self);
        }
    if (scaler !== null)
        scaler.limit = limit();
}

/* What the worklet played and heard, up to audio frame `frame'. */
function batch ({ frame, notes, inputs: heard, knobs: values })
{
    if (raster === null)
        return;

    origin ??= frame;

    const count = notes.length / RECORD;

    lastAt = frame - origin;
    for (let i = 0; i < count; i++)
        notes[i * RECORD] -= origin;
    inputs = heard;

    const turned = [];

    knobNames.forEach((name, k) =>
    {
        if (values[k] !== knobs[k])
            turned.push([`knob_${name}`, values[k]]);
    });
    knobs = [...values];

    if (count > 0)
    {
        M.HEAPF64.set(notes, M._tv_note_buffer(count) / 8);
        M._tv_notes(count);
    }
    M.HEAPF32.set(heard, M._tv_input_buffer() / 4);
    M._tv_inputs(heard.length - 4, lastAt);
    for (const [name, value] of turned)
        M.ccall('tv_set_at', null, ['string', 'number', 'number'], [name, value, lastAt]);
    M._tv_generated(lastAt);
    for (const w of helpers())
        w.worker.postMessage({ type: 'batch', at: lastAt, notes, inputs: heard, knobs: turned });
    soon();
}

/* The video frame on screen at the next refresh, or -1 before the page
   has said how its clock runs. */
function due ()
{
    if (clock === null || origin === null)
        return -1;

    const pageMs = performance.timeOrigin + performance.now() + AHEAD_MS - clock.timeOrigin;
    const heard = (pageMs / 1000 + clock.offset) * clock.rate;

    return Math.max(-1, Math.floor((heard - origin) * raster.fps / raster.rate));
}

/* RGBA at `at' in the module, frame `frame', to the page, if it has a
   buffer to spare. */
function show (frame, at)
{
    const bytes = 4 * raster.width * raster.height;

    if (pool.length + most - made <= 0)
        return;

    let buffer = pool.pop();

    if (buffer === undefined)
    {
        buffer = new ArrayBuffer(bytes);
        made++;
    }
    new Uint8Array(buffer).set(M.HEAPU8.subarray(at, at + bytes));
    post({ type: 'frame', at: origin + frame * raster.rate / raster.fps, buffer }, [buffer]);
}

function giveBack (w, buffer)
{
    if (w === self)
        ownPool.push(buffer);
    else
        w.worker.postMessage({ type: 'back', epoch, buffer }, [buffer]);
}

/* The workers a frame has a part from. */
const needed = (frame) => workers.filter((w) => w.ready && w.joinAt <= frame &&
                                                frame < w.leaveAt);

const complete = (frame) =>
{
    const got = parts.get(frame);

    return got !== undefined && needed(frame).every((w) => got.has(w));
};

/* The frame's parts summed in order, turned to RGBA, and shown; each
   part's buffer back to its worker. */
function compose (frame)
{
    const pixels = 3 * raster.width * raster.height;
    const into = M._tv_plane_buffer() / 4;
    const sum = M.HEAPF32.subarray(into, into + pixels);
    const got = parts.get(frame);
    let first = true;

    for (const w of needed(frame))
    {
        const p = new Float32Array(got.get(w));

        if (first)
            sum.set(p);
        else
            for (let i = 0; i < pixels; i++)
                sum[i] += p[i];
        first = false;
    }
    for (const [w, buffer] of got)
        giveBack(w, buffer);
    parts.delete(frame);
    show(frame, M._tv_rgba());
}

/* This worker's own share, drawn while it has buffers to put it in. */
function drawHere ()
{
    const began = performance.now();
    const perFrame = Math.ceil(raster.height / raster.lines);
    const bytes = 12 * raster.width * raster.height;
    const free = () => ownPool.length + PART_BUFFERS - ownMade;
    let count = 0;

    while (free() >= 2 && performance.now() - began < SLICE_MS)
    {
        count = M._tv_render(-1, perFrame);
        for (let i = 0; i < count; i++)
        {
            let buffer = ownPool.pop();

            if (buffer === undefined)
            {
                buffer = new ArrayBuffer(bytes);
                ownMade++;
            }

            const at = M._tv_done_rgba(i);
            const frame = M._tv_done_frame(i);

            new Uint8Array(buffer).set(M.HEAPU8.subarray(at, at + bytes));
            if (frame < next)
                ownPool.push(buffer);
            else
            {
                if (!parts.has(frame))
                    parts.set(frame, new Map());
                parts.get(frame).set(self, buffer);
                self.last = performance.now();
            }
        }
        M._tv_done_clear();
        if (count === 0)
            break;
    }
    spent += performance.now() - began;
    return count > 0 && free() >= 2;
}

/* Each frame once all its parts are in; a skip, the same for every
   worker, when the picture has fallen more than a frame behind. */
function lead ()
{
    const began = performance.now();
    const reachable = Math.floor(lastAt * raster.fps / raster.rate);
    const d = due();
    const to = d < reachable ? d : reachable;

    /* How long a helper has been quiet longer than the worker heard from
       last. Waiting for audio, a slow machine or this worker held up quiet
       every worker alike; one quiet alone has stalled, which STALL_MS
       judges. Until then the page isn't told frames are dropping, which
       would shrink a picture this machine can draw. */
    const heard = (w) =>
    {
        let held = 0;

        for (const got of parts.values())
            held += got.has(w) ? 1 : 0;

        /* One whose buffers are all here waits on this worker, not
           quiet: it draws only with two free. */
        return held > PART_BUFFERS - 2 ? began : w.last;
    };
    const newest = Math.max(...workers.filter((w) => w.ready).map(heard));
    const quiet = (w) => (w.ready ? newest - heard(w) : 0);
    /* Quiet for a few of its own frames is slow, not stalled. */
    const recent = (w) => quiet(w) <= 3 * Math.max(w.gap, 1000 / raster.fps);
    const stalled = (w) => (w.ready ? quiet(w) > Math.max(STALL_MS, 4 * w.gap)
                                    : began - w.started > START_MS);

    /* Frames older than the one due are late whether or not their parts
       are in. */
    if (d >= 0 && to > next + 1)
    {
        M._tv_skip_to(to);
        for (const w of helpers())
            if (w.ready)
                w.worker.postMessage({ type: 'skip', frame: Math.max(to, w.joinAt) });
        for (const [frame, got] of parts)
            if (frame < to)
            {
                for (const [w, buffer] of got)
                    giveBack(w, buffer);
                parts.delete(frame);
            }
        next = to;
        skipped++;
        /* Behind is another worker's to answer while one could be added. */
        if (!settling && !scaler.canGrow(active().length) && workers.every(recent))
            post({ type: 'dropping' });
    }
    while (complete(next))
        compose(next++);
    if (next >= reachable)
        settling = false;

    /* A helper let go once its frames have all been composed. */
    for (const w of helpers())
        if (next >= w.leaveAt)
        {
            for (const got of parts.values())
                if (got.has(w))
                {
                    giveBack(w, got.get(w));
                    got.delete(w);
                }
            w.worker.terminate();
            workers.splice(workers.indexOf(w), 1);
        }

    const late = helpers().find(stalled);

    if (late !== undefined)
    {
        lose(late);
        return;
    }
    if (began - since >= 1000)
        second(began);
}

/* Once a second: how busy, whether to add a worker or let one go, and
   which silent channels to move. */
function second (now)
{
    self.busy = spent / (now - since);
    self.silent = new Set([...self.fading].filter((ch) => !M._tv_sounding(ch)));

    const drawing = active();
    const busy = Math.max(...drawing.map((w) => w.busy));

    post({ type: 'busy', fraction: busy });
    scaler.limit = limit();

    /* Counted with those still starting, so one on its way isn't asked
       for again. */
    const counted = workers.filter((w) => !w.retiring).length;
    const change = scaler.tick({ busy, skips: skipped, workers: counted, now: now / 1000 });

    spent = 0;
    skipped = 0;
    since = now;
    if (change > 0)
        addHelper();
    else if (change < 0 || counted > limit())
    {
        const go = helpers().filter((w) => !w.retiring && w.joinAt !== Infinity)
                            .sort((a, b) => a.share.size - b.share.size)[0];

        if (go !== undefined)
            go.retiring = now;
    }
    move(now);
    say();
}

/* A channel handed on, once its voices have faded where it was, drawn
   there no more; a helper let go hands on all it has, and leaves once they
   have faded, or after FORCE_MS every worker is opened again without it;
   and a channel from the fullest worker to the emptiest. */
function move (now)
{
    for (const w of workers)
        for (const ch of [...w.fading])
            if (w.silent.has(ch))
            {
                w.fading.delete(ch);
                w.silent.delete(ch);
                drop(w, ch);
            }
    for (const w of helpers().filter((o) => o.retiring && o.leaveAt === Infinity))
    {
        for (const ch of [...w.share])
        {
            const to = fewest(ch, w);

            if (to !== undefined)
                place(ch, to);
        }
        if (w.share.size === 0 && w.fading.size === 0)
            w.leaveAt = next + JOIN_AHEAD;
        else if (now - w.retiring > FORCE_MS)
        {
            reopen(active().length);
            return;
        }
    }

    const room = active();

    if (room.length < 2)
        return;

    const full = room.reduce((a, b) => (b.share.size > a.share.size ? b : a));
    const ch = [...full.share][0];
    const empty = fewest(ch, full);

    if (empty !== undefined && full.share.size - empty.share.size >= 2)
        place(ch, empty);
}

function draw ()
{
    pending = false;
    if (raster === null || origin === null)
        return;

    const more = drawHere();

    lead();

    const narrow = M.ccall('tv_narrow', 'string', [], []);

    if (narrow !== '' && !narrowSaid)
    {
        narrowSaid = true;
        post({ type: 'narrow', text: narrow });
    }
    if (more || complete(next))
        soon();
}

/* A message to itself rather than setTimeout(0), whose chain the browser
   stretches to 4 ms a link -- half the time idle on a machine behind. */
const wake = new MessageChannel();

wake.port1.onmessage = draw;

function soon ()
{
    if (!pending)
    {
        pending = true;
        wake.port2.postMessage(0);
    }
}

/* The page's set: at the audio reached, so every worker sets it on one
   line. */
function set (name, value)
{
    M.ccall('tv_set_at', null, ['string', 'number', 'number'], [name, value, lastAt]);
    for (const w of helpers())
        w.worker.postMessage({ type: 'set', name, value, at: lastAt });
}

function receive (m)
{
    if (m.type === 'start')
    {
        start(m);
        return;
    }
    if (M === null)
    {
        early.push(m);
        return;
    }

    switch (m.type)
    {
    case 'open':
        open(m);
        break;
    case 'channels':
        channels(m.list);
        break;
    case 'knobs':
        knobNames = m.names;
        break;
    case 'set':
        pageSets.set(m.name, m.value);
        set(m.name, m.value);
        break;
    case 'clock':
        clock = { offset: m.offset, timeOrigin: m.timeOrigin, rate: m.rate };
        break;
    case 'batch':
        batch(m);
        break;
    case 'back':
        if (raster !== null && pool.length < most &&
            m.buffer.byteLength === 4 * raster.width * raster.height)
            pool.push(m.buffer);
        break;
    case 'port':
        /* The worklet's own end: batches come straight from the audio
           thread, not through the page's. */
        worklet = m.port;
        m.port.onmessage = (e) => receive(e.data);
        break;
    }
}

onmessage = (e) => receive(e.data);
