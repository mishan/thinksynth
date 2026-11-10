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

const post = (m, transfer) => postMessage(m, transfer ?? []);

/* The module the page compiled, instantiated here. */
async function start ({ module })
{
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

function open (m)
{
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
    for (const ch of was)
        follow(ch);

    /* Opened with nothing held: what is down already, after this. */
    worklet?.postMessage({ type: 'resend' });
}

function channels (list)
{
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

function draw ()
{
    pending = false;
    if (raster === null || origin === null)
        return;

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
        M.ccall('tv_set', null, ['string', 'number'], [m.name, m.value]);
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
