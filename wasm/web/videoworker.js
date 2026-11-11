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
 * Where the cores allow, it leads helpers (videopart.js) rather than
 * drawing: each follows some of the channels, sent every batch stamped
 * from this worker's start, and hands back float planes; this sums each
 * frame's parts in helper order, turns them to RGBA and decides every
 * skip, the same for all. A picture with an effect, or one channel, or a
 * machine with too few cores, is drawn here alone, as it always was.
 */

import createThinkWeb from './thinkweb.js';

/* twvideo.cpp's note record. */
const RECORD = 11;

/* How long one slice of drawing may take before messages are looked at. */
const SLICE_MS = 4;


/* A frame is wanted for the screen's next refresh, not this moment. */
const AHEAD_MS = 1000 / 60;

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
let pending = false, skips = 0;
/* Time spent drawing since `since', told to the page each second. */
let spent = 0, since = performance.now();
let settling = false;   /* catching up to now after an open */
let knobs = [];         /* their values as last set */
let worklet = null;     /* the worklet's port */

/* Helpers: one a core past the page's and the audio's, two at least or
   none, no more than the channels, and none for a picture with an effect.
   `cap' is how many a machine has shown it can run; `override' the
   page's. A helper that hands nothing back for STALL_MS longer than
   another is given up on. */
const MOST_HELPERS = 8, STALL_MS = 250;

/* How long a helper may take to start before it is given up on. */
const START_MS = 2000;

/* Frames a helper has out with this worker at most: videopart.js's MOST. */
const PART_BUFFERS = 4;
let module = null, helpers = [], epoch = 0, cap = MOST_HELPERS, override = null;
let lastOpen = null, next = 0, lastAt = 0, narrowSaid = false;
/* The page's own sets, put back when this worker opens again of itself
   (a helper given up on, a change of channels), which the page doesn't
   know to resend. */
const pageSets = new Map();
let patchSaid = false;
const parts = new Map();    /* frame -> each helper's planes */

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

function follow (channel)
{
    const why = M.ccall('tv_follow', 'string', ['number', 'string', 'string'],
                        [channel, patch.name, patch.text]);

    if (why !== '')
        post({ type: 'failed', text: why });
    else
        following.add(channel);
}

/* `again': opened by this worker rather than the page. */
function open (m, again = false)
{
    lastOpen = m;
    override = m.helpers ?? override;
    lastAt = 0;
    if (!again)
    {
        pageSets.clear();
        patchSaid = false;
    }
    stopHelpers();
    raster = { width: m.width, height: m.height, fps: m.fps, lines: m.lines,
               rate: m.rate };
    most = m.pool;
    settling = true;
    knobs = [];
    patch = m.patch;
    effect = m.fx ?? null;
    origin = null;
    skips = 0;
    pool.length = 0;
    made = 0;

    const why = M.ccall('tv_open', 'string',
                        ['number', 'number', 'number', 'number', 'number'],
                        [m.width, m.height, m.fps, m.lines, m.rate]);

    if (why !== '')
    {
        raster = null;
        post({ type: 'failed', text: why });
        return;
    }

    if (effect !== null)
    {
        const fxWhy = M.ccall('tv_effect', 'string', ['string', 'string'],
                              [effect.name, effect.text]);

        if (fxWhy !== '')
            post({ type: 'failed', text: fxWhy });
    }

    const was = following;

    following = new Set();
    if (wanted(was.size) >= 2)
        startHelpers(wanted(was.size), [...was]);
    else
        for (const ch of was)
            follow(ch);
    post({ type: 'workers', count: Math.max(1, helpers.length) });
    for (const [name, value] of pageSets)
        set(name, value);

    /* Opened with nothing held: what is down already, after this. */
    worklet?.postMessage({ type: 'resend' });
}

/* How many helpers `channels' followed would have. */
function wanted (channels)
{
    const cores = override ?? (navigator.hardwareConcurrency ?? 2) - 2;
    const n = Math.min(cores, cap, channels, MOST_HELPERS);

    return effect !== null || n < 2 ? 0 : n;
}

function stopHelpers ()
{
    for (const h of helpers)
        h.worker.terminate();
    helpers = [];
    parts.clear();
    next = 0;
    epoch++;
}

/* `n' helpers, `list' dealt among them, each to the one with fewest. */
function startHelpers (n, list)
{
    for (let k = 0; k < n; k++)
    {
        const worker = new Worker(new URL('videopart.js', import.meta.url), { type: 'module' });
        const h = { worker, share: new Set(), busy: 0, last: 0, ready: false,
                    started: performance.now(), gap: 1000 / raster.fps };

        worker.onmessage = (e) => fromHelper(h, e.data);
        worker.onerror = () => lose(h);
        helpers.push(h);
    }
    for (const ch of list)
        deal(ch);
    for (const h of helpers)
    {
        h.worker.postMessage({ type: 'start', module });
        h.worker.postMessage({ type: 'open', width: raster.width, height: raster.height,
                               fps: raster.fps, lines: raster.lines, rate: raster.rate,
                               patch, share: [...h.share], epoch });
    }
    for (const ch of list)
        following.add(ch);
}

function deal (channel)
{
    const h = helpers.reduce((a, b) => (b.share.size < a.share.size ? b : a));

    h.share.add(channel);
}

/* A helper given up on: every worker opened again, one helper fewer. */
function lose (h)
{
    if (!helpers.includes(h))
        return;
    cap = helpers.length - 1;
    open(lastOpen, true);
}

function fromHelper (h, m)
{
    if (!helpers.includes(h))
        return;
    switch (m.type)
    {
    case 'ready':
        h.ready = true;
        h.last = performance.now();
        break;
    case 'part':
        if (m.epoch !== epoch)
            return;
        /* Its own pace: how long it takes between parts, smoothed. */
        h.gap += (performance.now() - h.last - h.gap) / 8;
        h.last = performance.now();
        if (m.frame < next)
        {
            h.worker.postMessage({ type: 'back', epoch, buffer: m.buffer }, [m.buffer]);
            return;
        }
        if (!parts.has(m.frame))
            parts.set(m.frame, new Array(helpers.length).fill(null));
        parts.get(m.frame)[helpers.indexOf(h)] = m.buffer;
        soon();
        break;
    case 'busy':
        h.busy = m.fraction;
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
            lose(h);
        break;
    }
}

function channels (list)
{
    if (raster !== null && wanted(list.length) !== helpers.length)
    {
        following = new Set(list);
        open(lastOpen, true);
        return;
    }
    if (helpers.length > 0)
    {
        for (const h of helpers)
            for (const ch of h.share)
                if (!list.includes(ch))
                    h.share.delete(ch);
        for (const ch of list)
            if (!helpers.some((h) => h.share.has(ch)))
                deal(ch);
        for (const h of helpers)
            h.worker.postMessage({ type: 'share', list: [...h.share] });
        following = new Set(list);
        return;
    }
    for (const ch of following)
        if (!list.includes(ch))
        {
            M._tv_unfollow(ch);
            following.delete(ch);
        }

    for (const ch of list)
        if (!following.has(ch) && raster !== null)
            follow(ch);
        else
            following.add(ch);
}

/* What the worklet played and heard, up to audio frame `frame'. */
function batch ({ frame, notes, inputs, knobs: values })
{
    if (raster === null)
        return;

    origin ??= frame;

    const count = notes.length / RECORD;

    lastAt = frame - origin;
    if (helpers.length > 0)
    {
        for (let i = 0; i < count; i++)
            notes[i * RECORD] -= origin;

        const turned = [];

        knobNames.forEach((name, k) =>
        {
            if (values[k] !== knobs[k])
                turned.push([`knob_${name}`, values[k]]);
        });
        knobs = [...values];
        for (const h of helpers)
            h.worker.postMessage({ type: 'batch', at: lastAt, notes, inputs, knobs: turned });
        soon();
        return;
    }

    if (count > 0)
    {
        const at = M._tv_note_buffer(count) / 8;

        M.HEAPF64.set(notes, at);
        for (let i = 0; i < count; i++)
            M.HEAPF64[at + i * RECORD] -= origin;
        M._tv_notes(count);
    }

    M.HEAPF32.set(inputs, M._tv_input_buffer() / 4);
    M._tv_inputs(inputs.length - 4, frame - origin);

    knobNames.forEach((name, k) =>
    {
        if (values[k] !== knobs[k])
            M.ccall('tv_set_at', null, ['string', 'number', 'number'],
                    [`knob_${name}`, values[k], frame - origin]);
    });
    knobs = [...values];

    M._tv_generated(frame - origin);
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

/* The finished frames to the page, the newest where there are not
   buffers for all. */
function hand (count)
{
    const bytes = 4 * raster.width * raster.height;
    const room = pool.length + most - made;

    for (let i = Math.max(0, count - room); i < count; i++)
    {
        let buffer = pool.pop();

        if (buffer === undefined)
        {
            buffer = new ArrayBuffer(bytes);
            made++;
        }

        const at = M._tv_done_rgba(i);
        const frame = M._tv_done_frame(i);

        new Uint8Array(buffer).set(M.HEAPU8.subarray(at, at + bytes));
        post({ type: 'frame', at: origin + frame * raster.rate / raster.fps, buffer },
             [buffer]);
    }
    M._tv_done_clear();
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

/* The frame's parts summed in helper order, turned to RGBA, and shown;
   each part's buffer back to its helper. */
function compose (frame)
{
    const pixels = 3 * raster.width * raster.height;
    const into = M._tv_plane_buffer() / 4;
    const sum = M.HEAPF32.subarray(into, into + pixels);
    const got = parts.get(frame);

    got.forEach((buffer, k) =>
    {
        const p = new Float32Array(buffer);

        if (k === 0)
            sum.set(p);
        else
            for (let i = 0; i < pixels; i++)
                sum[i] += p[i];
        helpers[k].worker.postMessage({ type: 'back', epoch, buffer }, [buffer]);
    });
    parts.delete(frame);
    show(frame, M._tv_rgba());
}

const complete = (frame) => parts.get(frame)?.every((b) => b !== null) ?? false;

/* Led: each frame once all its parts are in, and a skip, the same for
   every helper, when the picture has fallen more than a frame behind. */
function lead ()
{
    const began = performance.now();
    const reachable = Math.floor(lastAt * raster.fps / raster.rate);
    const d = due();
    const to = d < reachable ? d : reachable;

    /* How long a helper has been quiet longer than the one heard from
       last. Waiting for audio, a slow machine or this worker held up quiet
       every helper alike; one quiet alone has stalled, which STALL_MS
       judges. Until then the page isn't told frames are dropping, which
       would shrink a picture this machine can draw. */
    const heard = (h) =>
    {
        const k = helpers.indexOf(h);
        let held = 0;

        for (const got of parts.values())
            held += got[k] !== null ? 1 : 0;

        /* One whose buffers are all here waits on this worker, not
           quiet: it draws only with two free (videopart.js). */
        return held > PART_BUFFERS - 2 ? began : h.last;
    };
    const newest = Math.max(...helpers.map(heard));
    const quiet = (h) => (h.ready ? newest - heard(h) : 0);
    /* Quiet for a few of its own frames is slow, not stalled. */
    const recent = (h) => quiet(h) <= 3 * Math.max(h.gap, 1000 / raster.fps);
    const stalled = (h) => (h.ready ? quiet(h) > Math.max(STALL_MS, 4 * h.gap)
                                    : began - h.started > START_MS);

    /* As a module alone skips: frames older than the one due are late
       whether or not their parts are in. */
    if (d >= 0 && to > next + 1)
    {
        for (const h of helpers)
            h.worker.postMessage({ type: 'skip', frame: to });
        for (const [frame, got] of parts)
            if (frame < to)
            {
                got.forEach((buffer, k) =>
                {
                    if (buffer !== null)
                        helpers[k].worker.postMessage({ type: 'back', epoch, buffer }, [buffer]);
                });
                parts.delete(frame);
            }
        next = to;
        if (!settling && helpers.every(recent))
            post({ type: 'dropping' });
    }
    while (complete(next))
        compose(next++);
    if (next >= reachable)
        settling = false;

    const now = performance.now();
    const late = helpers.find(stalled);

    if (now - since >= 1000)
    {
        post({ type: 'busy', fraction: Math.max(...helpers.map((h) => h.busy)) });
        since = now;
    }
    if (late !== undefined)
        lose(late);
}

function draw ()
{
    pending = false;
    if (raster === null || origin === null)
        return;
    if (helpers.length > 0)
    {
        lead();
        return;
    }

    const began = performance.now();
    const perFrame = Math.ceil(raster.height / raster.lines);
    let count;

    do
    {
        count = M._tv_render(due(), perFrame);
        hand(count);
    }
    while (count > 0 && performance.now() - began < SLICE_MS);

    const now = performance.now();

    spent += now - began;
    if (now - since >= 1000)
    {
        post({ type: 'busy', fraction: spent / (now - since) });
        spent = 0;
        since = now;
    }

    const narrow = M.ccall('tv_narrow', 'string', [], []);

    if (narrow !== '')
        post({ type: 'narrow', text: narrow });

    /* The batches that waited while the module compiled are a backlog,
       and the skip past them is not this machine falling behind. */
    if (M._tv_skips() > skips && !settling)
        post({ type: 'dropping' });
    skips = M._tv_skips();
    if (count === 0)
        settling = false;

    if (count > 0)
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

/* The page's set: led, at the audio reached, so every helper sets it on
   one line. */
function set (name, value)
{
    if (helpers.length > 0)
        for (const h of helpers)
            h.worker.postMessage({ type: 'set', name, value, at: lastAt });
    else
        M.ccall('tv_set', null, ['string', 'number'], [name, value]);
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
