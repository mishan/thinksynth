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
 * videopart.js -- some of a picture's channels, drawn for videoworker.js.
 *
 * Every voice draws every pixel, so a dense piece is more than one core can
 * draw. The video worker, leading, starts one of these for each core it can
 * spare and deals each some of the channels it follows; each draws only
 * those, as float planes, and hands each finished frame back for the leader
 * to sum with the others' and show.
 *
 * A part never decides anything about time. It is sent every batch the
 * leader is, stamped as the leader stamped it, and skips only when the
 * leader says to, to the frame it says, so every part of a frame is drawn
 * from the same notes on the same lines.
 */

import createThinkWeb from './thinkweb.js';

const RECORD = 11;
const SLICE_MS = 4;

/* Frames out with the leader at once; with none back, drawing waits. */
const MOST = 4;

let M = null;
const early = [];
let raster = null, epoch = 0, made = 0;
let following = new Set();
let pending = false, said = false;
let spent = 0, since = performance.now();
const pool = [];

const post = (m, transfer) => postMessage(m, transfer ?? []);

async function start ({ module })
{
    try
    {
        M = await createThinkWeb({
            instantiateWasm: (imports, done) =>
            {
                WebAssembly.instantiate(module, imports)
                    .then((instance) => done(instance, module))
                    .catch((e) => post({ type: 'failed', text: `a video part's module: ${e}` }));

                return {};
            },
            print: () => {},
            printErr: () => {},
        });
    }
    catch (e)
    {
        post({ type: 'failed', text: `a video part's module: ${e}` });
        return;
    }
    post({ type: 'ready' });
    for (const m of early.splice(0))
        receive(m);
}

function follow (channel, patch)
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
    raster = { width: m.width, height: m.height, fps: m.fps, lines: m.lines, rate: m.rate,
               patch: m.patch };
    epoch = m.epoch;
    pool.length = 0;
    made = 0;
    following = new Set();

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
    for (const ch of m.share)
        follow(ch, m.patch);
}

function share (list)
{
    for (const ch of following)
        if (!list.includes(ch))
        {
            M._tv_unfollow(ch);
            following.delete(ch);
        }
    for (const ch of list)
        if (!following.has(ch))
            follow(ch, raster.patch);
}

/* A batch as the leader stamped it: notes and `at' already in frames from
   the picture's start. */
function batch ({ at, notes, inputs, knobs })
{
    const count = notes.length / RECORD;

    if (count > 0)
    {
        M.HEAPF64.set(notes, M._tv_note_buffer(count) / 8);
        M._tv_notes(count);
    }
    M.HEAPF32.set(inputs, M._tv_input_buffer() / 4);
    M._tv_inputs(inputs.length - 4, at);
    for (const [name, value] of knobs)
        M.ccall('tv_set_at', null, ['string', 'number', 'number'], [name, value, at]);
    M._tv_generated(at);
    soon();
}

/* Buffers free: those back and those not yet made. A frame's windows
   finish at most two frames, one ending and one begun before it. */
const free = () => pool.length + MOST - made;

/* The finished frames to the leader. */
function hand (count)
{
    const bytes = 12 * raster.width * raster.height;

    for (let i = 0; i < count; i++)
    {
        let buffer = pool.pop();

        if (buffer === undefined)
        {
            buffer = new ArrayBuffer(bytes);
            made++;
        }

        const at = M._tv_done_rgba(i);

        new Uint8Array(buffer).set(M.HEAPU8.subarray(at, at + bytes));
        post({ type: 'part', epoch, frame: M._tv_done_frame(i), buffer }, [buffer]);
    }
    M._tv_done_clear();
}

function draw ()
{
    pending = false;
    if (raster === null || free() < 2)
        return;

    const began = performance.now();
    const perFrame = Math.ceil(raster.height / raster.lines);
    let count;

    do
    {
        count = M._tv_render(-1, perFrame);
        hand(count);
    }
    while (free() >= 2 && count > 0 && performance.now() - began < SLICE_MS);

    const now = performance.now();

    spent += now - began;
    if (now - since >= 1000)
    {
        post({ type: 'busy', fraction: spent / (now - since) });
        spent = 0;
        since = now;
    }

    const narrow = M.ccall('tv_narrow', 'string', [], []);

    if (narrow !== '' && !said)
    {
        said = true;
        post({ type: 'narrow', text: narrow });
    }
    if (free() >= 2 && count > 0)
        soon();
}

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
    case 'share':
        if (raster !== null)
            share(m.list);
        break;
    case 'set':
        M.ccall('tv_set_at', null, ['string', 'number', 'number'], [m.name, m.value, m.at]);
        break;
    case 'batch':
        if (raster !== null)
            batch(m);
        break;
    case 'skip':
        if (raster !== null)
        {
            M._tv_skip_to(m.frame);
            soon();
        }
        break;
    case 'back':
        if (raster !== null && m.epoch === epoch &&
            m.buffer.byteLength === 12 * raster.width * raster.height)
        {
            pool.push(m.buffer);
            soon();
        }
        break;
    }
}

onmessage = (e) => receive(e.data);
