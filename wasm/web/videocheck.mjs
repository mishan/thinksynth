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
 * videocheck.mjs -- the live video synth through the browser build, from
 * Node.
 *
 *   node wasm/web/videocheck.mjs [BUILD_DIR]
 *   node wasm/web/videocheck.mjs --bench [BUILD_DIR]
 *
 * What videoworker.js asks of the module (twvideo.cpp), with notes written
 * as the worklet writes them: a note lights the picture from the line it
 * sounded on; nothing is drawn past the audio generated; a picture asked
 * for a frame far ahead skips to it, drawing what is held there and
 * nothing that began and ended in between; a flush lets go; a note's aux
 * reaches the patch.
 *
 * --bench renders the shipped patches as a busy piece would ask, and says
 * how many times faster than real time each goes.
 *
 * Exit status is the number of failures.
 */

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const bench = process.argv[2] === '--bench';
const build = path.resolve(process.argv[bench ? 3 : 2] ??
                           path.join(here, '..', '..', 'build-web'));
const video = path.join(here, '..', '..', 'video');

const { default: createThinkWeb } =
    await import(pathToFileURL(path.join(build, 'thinkweb.js')).href);

const M = await createThinkWeb({ print: () => {}, printErr: () => {} });

let failures = 0;

function check (name, ok, why = '')
{
    console.log(`${ok ? 'ok  ' : 'FAIL'}  ${name}${ok ? '' : `\n  ${why}`}`);
    failures += ok ? 0 : 1;
}

const W = 160, H = 120, FPS = 60, RATE = 48000, LINES = 8;
const PER_FRAME = RATE / FPS;

/* twvideo.cpp's record: at, kind, channel, note, velocity, level, aux0-3. */
const OFF = 0, ON = 1, FLUSH = 2, RECORD = 10;

const call = (name, ret, types, args) => M.ccall(name, ret, types, args);

function open ()
{
    return call('tv_open', 'string', ['number', 'number', 'number', 'number', 'number'],
                [W, H, FPS, LINES, RATE]);
}

function follow (channel, name, text)
{
    return call('tv_follow', 'string', ['number', 'string', 'string'], [channel, name, text]);
}

function effect (name, text)
{
    return call('tv_effect', 'string', ['string', 'string'], [name, text]);
}

function notes (list)
{
    const records = new Float64Array(list.length * RECORD);

    list.forEach((n, i) => records.set([n.at, n.kind, n.channel ?? 0, n.note ?? 60,
                                        n.velocity ?? 127, 1, ...(n.aux ?? [0, 0, 0, 0])],
                                       i * RECORD));

    M.HEAPF64.set(records, M._tv_note_buffer(list.length) / 8);
    M._tv_notes(list.length);
}

function inputs (level)
{
    M.HEAPF32.set([level, 0, 0, 0], M._tv_input_buffer() / 4);
    M._tv_inputs(0);
}

/* Every frame finished, as { frame, rgba }, rendering all it can a frame's
   windows at a time, as videoworker.js takes them. */
function render (due = -1)
{
    const frames = [];

    /* Bounded, so a module that never stops rendering fails rather than
       hangs. */
    for (let calls = 0; calls < 1000; calls++)
    {
        const count = M._tv_render(due, H / LINES);

        for (let i = 0; i < count; i++)
        {
            const at = M._tv_done_rgba(i);

            frames.push({ frame: M._tv_done_frame(i),
                          rgba: M.HEAPU8.slice(at, at + 4 * W * H) });
        }
        M._tv_done_clear();
        if (count === 0)
            break;
    }

    return frames;
}

/* A patch that is one color while its note sounds: `rgb' a channel each,
   `release' how long it takes to go dark after. `voiced' lights it as long
   as the voice lives, release and all, at full strength whatever the
   envelope's level. */
function flat (rgb, release = '0 ms', voiced = false)
{
    const lit = voiced ? '1' : 'env->out';

    return `node ionode { channels = 3; out0 = ${lit} * ${rgb[0]};
             out1 = ${lit} * ${rgb[1]}; out2 = ${lit} * ${rgb[2]};
             play = env->play; };
            node env env::adsr { a = 0 ms; d = 0 ms; s = 1; r = ${release}; p = 1;
                                 trigger = ionode->trigger; };
            io ionode;`;
}

const pixel = (f, x, y, c) => f.rgba[4 * (y * W + x) + c];
const lit = (f, y, c = 0) => pixel(f, W / 2, y, c) > 128;

if (bench)
{
    const patches = ['moire.dsp', 'rings.dsp'];
    const SECONDS = 3, CHANNELS = 4, VOICES = 2;

    for (const fx of ['', 'fx/feedback.dsp'])
        for (const name of patches)
        {
            open();
            for (let ch = 0; ch < CHANNELS; ch++)
                follow(ch, name, fs.readFileSync(path.join(video, name), 'utf8'));
            if (fx)
                effect(fx, fs.readFileSync(path.join(video, fx), 'utf8'));
            inputs(0.2);
            notes(Array.from({ length: CHANNELS * VOICES },
                             (_, i) => ({ at: 0, kind: ON, channel: i % CHANNELS,
                                          note: 48 + 7 * i })));
            M._tv_generated(SECONDS * RATE);

            const t0 = performance.now();

            render();

            const took = (performance.now() - t0) / 1000;

            console.log(`${name}${fx ? ' + ' + fx : ''}: ${CHANNELS} channels, ` +
                        `${CHANNELS * VOICES} voices, ${(SECONDS / took).toFixed(2)}x real time`);
        }
    process.exit(0);
}

/* A note struck half way through frame 10. */
{
    open();
    follow(0, 'white', flat([1, 1, 1]));
    notes([{ at: 10.5 * PER_FRAME, kind: ON }]);
    M._tv_generated(12 * PER_FRAME);

    const frames = render();
    const f = (k) => frames.find((x) => x.frame === k);
    const first = f(10) && [...Array(H).keys()].find((y) => lit(f(10), y));

    check('a note lights the picture from the line it sounded on',
          frames.length === 12 && !lit(f(9), H - 1) && Math.abs(first - H / 2) <= LINES &&
          lit(f(11), 0),
          `${frames.length} frames; frame 10 lit from line ${first}`);
}

/* Nothing past the audio. */
{
    open();
    follow(0, 'white', flat([1, 1, 1]));
    M._tv_generated(2.5 * PER_FRAME);

    const frames = render();

    check('frames are drawn only as far as the audio is generated',
          frames.length === 2 && frames[1].frame === 1,
          `${frames.length} frames, the last ${frames.at(-1)?.frame}`);
}

/* Far behind: a red note struck and let go in the gap, and a green one
   still held, both voices living two seconds past their release. */
{
    open();
    follow(0, 'red', flat([1, 0, 0], '2 s', true));
    follow(1, 'green', flat([0, 1, 0], '2 s', true));
    notes([{ at: 6 * PER_FRAME, kind: ON, channel: 0 },
           { at: 9 * PER_FRAME, kind: ON, channel: 1 },
           { at: 12 * PER_FRAME, kind: OFF, channel: 0 }]);
    M._tv_generated(60 * PER_FRAME);

    const frames = render(50);
    const skips = M._tv_skips();

    check('a picture far behind skips to the frame that is due',
          frames[0]?.frame === 50 && skips === 1,
          `first frame ${frames[0]?.frame}, ${skips} skips`);
    check('the note held there draws, and the one that ended in the gap does not',
          frames[0] && lit(frames[0], 0, 1) && pixel(frames[0], 0, 0, 0) === 0,
          frames[0] ? `red ${pixel(frames[0], 0, 0, 0)}, green ${pixel(frames[0], 0, 0, 1)}`
                    : 'no frame');
}

/* Asked for a frame past the audio: to the frame the audio is in, and that
   frame handed over once the audio reaches its end. */
{
    open();
    follow(0, 'white', flat([1, 1, 1]));
    M._tv_generated(20.5 * PER_FRAME);

    const early = render(1000);

    M._tv_generated(21.5 * PER_FRAME);

    const frames = render(1000);

    check('a skip goes no further than the audio generated',
          early.length === 0 && frames.length === 1 && frames[0].frame === 20,
          `${early.length} frames, then ${frames.map((f) => f.frame)}`);
}

/* A note held across the gap, let go and struck again inside it, softer:
   the picture is the new striking's. */
{
    open();
    follow(0, 'level', flat(['ionode->velocity', 0, 0]));
    notes([{ at: 0, kind: ON, velocity: 127 },
           { at: 20 * PER_FRAME, kind: OFF },
           { at: 21 * PER_FRAME, kind: ON, velocity: 64 }]);
    M._tv_generated(2 * PER_FRAME);
    render();
    M._tv_generated(60 * PER_FRAME);

    const frames = render(50);
    const red = frames[0] && pixel(frames[0], W / 2, H / 2, 0);

    check('a note struck again in the gap draws as struck again',
          frames[0]?.frame === 50 && Math.abs(red - 129) <= 2,
          `frame ${frames[0]?.frame}, red ${red}`);
}

/* A flush. */
{
    open();
    follow(0, 'white', flat([1, 1, 1], '50 ms'));
    notes([{ at: 0, kind: ON }, { at: 5 * PER_FRAME, kind: FLUSH, channel: -1 }]);
    M._tv_generated(20 * PER_FRAME);

    const frames = render();

    check('a flush lets go of what is held',
          lit(frames[2], 0) && !lit(frames[19], 0),
          `frame 2 ${pixel(frames[2], W / 2, 0, 0)}, frame 19 ${pixel(frames[19], W / 2, 0, 0)}`);
}

/* A white flash through feedback, on the whole picture: dark again by
   frame 12 without it, a fading trail there with it. And the effect sees
   the whole picture, not each channel's: a light on one channel and its
   negative on another sum to nothing, and leave no trail. */
for (const fx of [false, true])
{
    open();
    follow(0, 'white', flat([1, 1, 1]));
    if (fx)
        check('the effect loads',
              effect('fx/feedback.dsp', fs.readFileSync(path.join(video, 'fx/feedback.dsp'),
                                                        'utf8')) === '');
    notes([{ at: 0, kind: ON }, { at: 5 * PER_FRAME, kind: OFF }]);
    M._tv_generated(13 * PER_FRAME);

    const frames = render();
    const left = frames[12] && pixel(frames[12], W / 2, H / 2, 0);

    check(fx ? 'feedback leaves a trail after the note' : 'without it, nothing is left',
          fx ? left > 0 : left === 0, `frame 12 red ${left}`);
}

{
    open();
    follow(0, 'white', flat([1, 1, 1]));
    follow(1, 'black', flat([-1, -1, -1]));
    effect('fx/feedback.dsp', fs.readFileSync(path.join(video, 'fx/feedback.dsp'), 'utf8'));
    notes([{ at: 0, kind: ON, channel: 0 }, { at: 0, kind: ON, channel: 1 },
           { at: 5 * PER_FRAME, kind: OFF, channel: 0 },
           { at: 5 * PER_FRAME, kind: OFF, channel: 1 }]);
    M._tv_generated(13 * PER_FRAME);

    const frames = render();
    const left = frames[12] && pixel(frames[12], W / 2, H / 2, 0);

    check('one trail for the whole picture: a light and its negative leave none',
          left === 0, `frame 12 red ${left}`);
}

/* aux. */
{
    open();
    follow(0, 'aux', `node ionode { channels = 3; out0 = env->out * ionode->aux1;
                       out1 = 0; out2 = 0; play = env->play; };
                      node env env::adsr { a = 0 ms; d = 0 ms; s = 1; r = 0 ms; p = 1;
                                           trigger = ionode->trigger; };
                      io ionode;`);
    notes([{ at: 0, kind: ON, aux: [0, 0.5, 0, 0] }]);
    M._tv_generated(2 * PER_FRAME);

    const frames = render();
    const red = frames[1] && pixel(frames[1], 0, 0, 0);

    check("a note's aux reaches the patch", Math.abs(red - 128) <= 1, `red ${red}`);
}

/* A patch that is not a picture. */
{
    open();

    const why = follow(0, 'broken', 'node ionode { out0 = nope->out; }; io ionode;');

    check('a patch that does not load says so', why === "'broken' did not load", why);
}

process.exit(failures);
