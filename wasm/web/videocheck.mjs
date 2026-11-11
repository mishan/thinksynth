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
 * nothing that began and ended in between; a flush lets go; the sound's
 * level and a knob land at their time, however early their batch; a note's aux
 * reaches the patch; every shipped patch draws a held note and goes dark
 * after; and every shipped patch and effect runs compiled, drawing what a
 * module that interprets them draws.
 *
 * --bench renders the shipped patches as a busy piece would ask, and says
 * how many times faster than real time each goes; with --interpret, on
 * their nodes.
 *
 * Exit status is the number of failures.
 */

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const flags = process.argv.slice(2).filter((a) => a.startsWith('--'));
const bench = flags.includes('--bench');
const build = path.resolve(process.argv.slice(2).find((a) => !a.startsWith('--')) ??
                           path.join(here, '..', '..', 'build-web'));
const video = path.join(here, '..', '..', 'video');

const { default: createThinkWeb } =
    await import(pathToFileURL(path.join(build, 'thinkweb.js')).href);

/* `M' is the module the helpers below drive. */
let M = await createThinkWeb({ print: () => {}, printErr: () => {} });

if (bench && flags.includes('--interpret'))
    M._tv_interpret();

let failures = 0;

function check (name, ok, why = '')
{
    console.log(`${ok ? 'ok  ' : 'FAIL'}  ${name}${ok ? '' : `\n  ${why}`}`);
    failures += ok ? 0 : 1;
}

const W = 160, H = 120, FPS = 60, RATE = 48000, LINES = 8;
const PER_FRAME = RATE / FPS;

/* twvideo.cpp's record: at, kind, channel, note, velocity, level, aux0-3,
   voice. */
const OFF = 0, ON = 1, FLUSH = 2, RECORD = 11;

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
                                        n.velocity ?? 127, 1, ...(n.aux ?? [0, 0, 0, 0]),
                                        n.voice ?? 0],
                                       i * RECORD));

    M.HEAPF64.set(records, M._tv_note_buffer(list.length) / 8);
    M._tv_notes(list.length);
}

/* What the sound says, as of audio frame `at'. */
function inputs (level, bass = 0, mid = 0, high = 0, at = 0)
{
    M.HEAPF32.set([level, bass, mid, high], M._tv_input_buffer() / 4);
    M._tv_inputs(0, at);
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

/* The same, from a module drawing float planes (tv_planes): each frame's
   R, G and B, unclamped, as one Float32Array. */
function renderPlanes ()
{
    const frames = [];

    for (let calls = 0; calls < 1000; calls++)
    {
        const count = M._tv_render(-1, H / LINES);

        for (let i = 0; i < count; i++)
        {
            const at = M._tv_done_rgba(i);

            frames.push({ frame: M._tv_done_frame(i),
                          planes: new Float32Array(M.HEAPU8.slice(at, at + 12 * W * H).buffer) });
        }
        M._tv_done_clear();
        if (count === 0)
            break;
    }

    return frames;
}

/* `planes' as tv_rgba makes them RGBA. */
function rgba (planes)
{
    M.HEAPF32.set(planes, M._tv_plane_buffer() / 4);

    const at = M._tv_rgba();

    return M.HEAPU8.slice(at, at + 4 * W * H);
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

/* How bright a frame's brightest pixel is, 0-765: a star is a few. */
const peak = (f) =>
{
    let most = 0;

    for (let i = 0; i < f.rgba.length; i += 4)
        most = Math.max(most, f.rgba[i] + f.rgba[i + 1] + f.rgba[i + 2]);
    return most;
};

/* The shipped patches, as the page lists them. */
const patches = fs.readdirSync(video).filter((n) => n.endsWith('.dsp')).sort();
const lit = (f, y, c = 0) => pixel(f, W / 2, y, c) > 128;

if (bench)
{
    const SECONDS = 3, CHANNELS = 4, VOICES = 2;

    for (const fx of ['', ...fs.readdirSync(path.join(video, 'fx')).filter((n) => n.endsWith('.dsp')).sort().map((n) => `fx/${n}`)])
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

/* Every shipped patch: it loads, draws in color, lights while a note is
   held with the music at a typical level, loud in the bass and the highs,
   and goes dark again within ten seconds of the note's end. */
check('the shipped patches are found', patches.length > 0, video);
for (const name of patches)
{
    open();

    const why = follow(0, name, fs.readFileSync(path.join(video, name), 'utf8'));

    inputs(0.15, 0.3, 0.1, 0.3);
    notes([{ at: 30 * PER_FRAME, kind: ON, note: 60 }]);
    M._tv_generated(150 * PER_FRAME);

    const held = render().at(-1);

    notes([{ at: 150 * PER_FRAME, kind: OFF, note: 60 }]);
    M._tv_generated(750 * PER_FRAME);

    const after = render().at(-1);
    const narrow = call('tv_narrow', 'string', [], []);

    check(`${name} draws a held note and goes dark after`,
          why === '' && narrow === '' && peak(held) > 64 && peak(after) === 0,
          `${why}${narrow} held ${peak(held)}, after ${peak(after)}`);
}

/* Every shipped patch alone and every shipped effect over rings.dsp, held
   and let go: compiled, and the same frames as on its nodes. */
{
    const compiled = M;
    const nodes = await createThinkWeb({ print: () => {}, printErr: () => {} });

    nodes._tv_interpret();

    let why;
    const play = (patch, fx) =>
    {
        open();

        const said = follow(0, patch, fs.readFileSync(path.join(video, patch), 'utf8')) +
                     (fx ? effect(fx, fs.readFileSync(path.join(video, fx), 'utf8')) : '');

        why += said && `${said}; `;
        if (said !== '')
            return [];
        inputs(0.15, 0.3, 0.1, 0.3);
        notes([{ at: 0, kind: ON, note: 60, aux: [0.5, 0.25, 0, 1] },
               { at: 40 * PER_FRAME, kind: OFF, note: 60 }]);
        M._tv_generated(60 * PER_FRAME);

        return render();
    };
    const fxs = fs.readdirSync(path.join(video, 'fx')).filter((n) => n.endsWith('.dsp'))
                  .sort().map((n) => `fx/${n}`);

    for (const [patch, fx] of [...patches.map((p) => [p, '']),
                               ...fxs.map((f) => ['rings.dsp', f])])
    {
        const name = fx ? `rings.dsp under ${fx}` : patch;
        why = '';

        const before = [M._tv_windows(0), M._tv_windows(1)];
        const drawn = play(patch, fx);
        const interpreted = M._tv_windows(0) - before[0];
        const ran = M._tv_windows(1) - before[1];

        M = nodes;

        const was = M._tv_windows(1);
        const expected = play(patch, fx);
        const reference = M._tv_windows(1) - was;

        M = compiled;

        const differs = expected.findIndex((f, i) => drawn[i] === undefined ||
                                           f.frame !== drawn[i].frame ||
                                           Buffer.compare(f.rgba, drawn[i].rgba) !== 0);

        check(`${name} runs compiled and draws what its nodes draw`,
              why === '' && interpreted === 0 && ran > 0 && reference === 0 &&
              drawn.length === 60 && expected.length === 60 && differs < 0 &&
              peak(drawn[30]) > 64,
              `${why}${ran} windows compiled, ${interpreted} on its nodes, ` +
              `${reference} compiled on the other; ${drawn.length} frames, ` +
              `peak ${drawn[30] ? peak(drawn[30]) : '-'}, ` +
              `${expected.length} on its nodes, frame ${differs} differs`);
    }
}

/* The sound's level, and a knob, turned up at frame 20: the same frames
   whether its batch arrives after the picture has drawn that far or before
   it has drawn anything. */
for (const [what, read, turn] of [
    ['the sound\'s level', '@in_level', (v, at) => inputs(v, 0, 0, 0, at)],
    ['a knob', '@knob_x', (v, at) => call('tv_set_at', null, ['string', 'number', 'number'],
                                         ['knob_x', v, at])]])
{
    const patch = `${read} = 0;
                   node ionode { channels = 3; out0 = ${read}; out1 = ${read};
                                 out2 = ${read}; play = 1; };
                   io ionode;`;
    const run = (early) =>
    {
        open();
        follow(0, 'level', patch);
        notes([{ at: 0, kind: ON }]);
        turn(0.2, 0);

        const frames = [];

        if (!early)
        {
            M._tv_generated(20 * PER_FRAME);
            frames.push(...render());
        }
        turn(0.8, 20 * PER_FRAME);
        M._tv_generated(40 * PER_FRAME);
        frames.push(...render());

        return frames;
    };
    const late = run(false), early = run(true);
    const differs = early.findIndex((f, i) => late[i] === undefined || f.frame !== late[i].frame ||
                                    Buffer.compare(f.rgba, late[i].rgba) !== 0);

    check(`${what} lands at its time, however early its batch arrives`,
          early.length === 40 && late.length === 40 && differs < 0 &&
          pixel(early[19], 0, 0, 0) === 51 && pixel(early[20], 0, 0, 0) === 204,
          `${early.length} and ${late.length} frames; frame ${differs} differs; ` +
          `frames 19 and 20 at ${early[19] && pixel(early[19], 0, 0, 0)}, ` +
          `${early[20] && pixel(early[20], 0, 0, 0)}`);
}

/* Every shipped patch on four channels, drawn whole by one module and in
   two parts, channels 0 and 2 and channels 1 and 3, by two drawing planes:
   each part is what a module following only its channels draws, byte for
   byte, and the parts summed are the whole within a step. */
{
    const whole = M;
    const parts = [await createThinkWeb({ print: () => {}, printErr: () => {} }),
                   await createThinkWeb({ print: () => {}, printErr: () => {} })];
    const shares = [[0, 2], [1, 3]];
    const play = (patch, channels) =>
    {
        open();
        for (const ch of channels)
            follow(ch, patch, fs.readFileSync(path.join(video, patch), 'utf8'));
        inputs(0.15, 0.3, 0.1, 0.3);
        notes([0, 1, 2, 3].flatMap((ch) => [
            { at: ch * PER_FRAME, kind: ON, channel: ch, note: 48 + 7 * ch },
            { at: (ch + 3) * PER_FRAME, kind: ON, channel: ch, note: 55 + 7 * ch },
            { at: 30 * PER_FRAME, kind: OFF, channel: ch, note: 48 + 7 * ch }]));
        M._tv_generated(45 * PER_FRAME);
    };

    for (const patch of patches)
    {
        M = whole;
        play(patch, [0, 1, 2, 3]);

        const drawn = render();
        const each = shares.map((share, k) =>
        {
            M = whole;
            play(patch, share);

            const alone = render();

            M = parts[k];
            M._tv_planes(1);
            play(patch, share);

            return { alone, planes: renderPlanes() };
        });

        M = whole;

        let why = '';
        const exact = each.every(({ alone, planes }) =>
            alone.length === planes.length &&
            planes.every((p, i) => Buffer.compare(rgba(p.planes), alone[i].rgba) === 0));

        if (!exact)
            why = 'a part is not what a module following its channels draws';
        for (let f = 0; f < drawn.length && !why; f++)
        {
            const sum = new Float32Array(3 * W * H);

            for (const { planes } of each)
                for (let i = 0; i < sum.length; i++)
                    sum[i] += planes[f].planes[i];

            const composed = rgba(sum);

            for (let i = 0; i < composed.length && !why; i++)
                if (Math.abs(composed[i] - drawn[f].rgba[i]) > 1)
                    why = `frame ${f}, byte ${i}: ${composed[i]} composed, ${drawn[f].rgba[i]} whole`;
        }
        check(`${patch} drawn in two parts sums to the whole`,
              why === '' && drawn.length === 45 && peak(drawn[20]) > 64,
              why || `${drawn.length} frames, peak ${drawn[20] && peak(drawn[20])}`);
    }
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

/* A batch out of time order, as a MIDI-out channel's later stamps make one:
   the earlier note is not held up behind the later. */
{
    open();
    follow(0, 'white', flat([1, 1, 1]));
    notes([{ at: 10 * PER_FRAME, kind: ON, note: 61 }, { at: 2 * PER_FRAME, kind: ON }]);
    M._tv_generated(4 * PER_FRAME);

    const frames = render();

    check('a note stamped before one ahead of it in its batch draws on time',
          frames.length === 4 && lit(frames[3], 0), `${frames.length} frames`);
}

/* A key struck again before the off of its first voice arrives, as a
   channel moved off a device has it: that off ends nothing. */
{
    open();
    follow(0, 'white', flat([1, 1, 1]));
    notes([{ at: 0, kind: ON, voice: 1 }, { at: PER_FRAME, kind: ON, voice: 2 },
           { at: 2 * PER_FRAME, kind: OFF, voice: 1 }]);
    M._tv_generated(4 * PER_FRAME);

    const frames = render();

    check("an off for a key's earlier voice does not end the one struck since",
          frames.length === 4 && lit(frames[3], 0), `${frames.length} frames`);
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

/* Soften: the left half lit and the right dark, a hard edge between, goes
   from lit to black over a few pixels. Only around the edge: the left
   column is the line above's end, wrapped. */
for (const fx of [false, true])
{
    open();
    follow(0, 'half', `node ionode { channels = 3; out0 = lit->out; out1 = lit->out;
                        out2 = lit->out; play = env->play; };
                       node s misc::scan { };
                       node lit math::clamp { in = (0.5 - s->x) * 1000 * env->out;
                                              lo = 0; hi = 1; };
                       node env env::adsr { a = 0 ms; d = 0 ms; s = 1; r = 0 ms; p = 1;
                                            trigger = ionode->trigger; };
                       io ionode;`);
    if (fx)
        effect('fx/soften.dsp', fs.readFileSync(path.join(video, 'fx/soften.dsp'), 'utf8'));
    notes([{ at: 0, kind: ON }]);
    M._tv_generated(3 * PER_FRAME);

    const frame = render()[2];
    const row = [...Array(20).keys()].map((i) => pixel(frame, W / 2 - 10 + i, H / 2, 0));
    const between = row.filter((v) => v > 10 && v < 245).length;

    check(fx ? 'soften grades a hard edge over a few pixels' : 'without it, the edge is hard',
          fx ? between >= 2 && between <= 4 : between === 0, `${between} between: ${row}`);
}

/* Palette: black stays black, and a mid gray leans to the palette's
   middle color, Ember's red or Ice's blue. */
for (const [n, name, warm] of [[0, 'Ember', true], [1, 'Ice', false]])
{
    open();
    follow(0, 'gray', flat([0.5, 0.5, 0.5]));
    effect('fx/palette.dsp', fs.readFileSync(path.join(video, 'fx/palette.dsp'), 'utf8'));
    call('tv_set', null, ['string', 'number'], ['palette', n]);
    notes([{ at: 0, kind: ON }, { at: 2 * PER_FRAME, kind: OFF }]);
    M._tv_generated(4 * PER_FRAME);

    const frames = render();
    const [r, g, b] = [0, 1, 2].map((c) => pixel(frames[1], W / 2, H / 2, c));

    check(`palette ${n} is ${name}: a mid gray turns ${warm ? 'red' : 'blue'}, black stays black`,
          (warm ? r - b > 60 : b - r > 60) && peak(frames[3]) === 0,
          `gray ${r},${g},${b}; after ${peak(frames[3])}`);
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

/* ---- the worklet's side ------------------------------------------------
 *
 * What a worklet's instance of the module (thinkweb.cpp, tw_video) hands the
 * worker: every note it sounds, with the frame it sounds at, and what the
 * sound says. Each in an instance of its own, driven as worklet.js drives
 * one: 128-frame quanta, drained every four. */

const WINDOW = 256, QUANTUM = 128, EVERY = 4;
const TW_SEEK = 13;

async function worklet ()
{
    const W = await createThinkWeb({ print: () => {}, printErr: () => {} });

    W._tw_create(RATE, WINDOW, QUANTUM);
    W._tw_video(1, 0);
    take(W);

    return W;
}

/* What tw_video_take hands over, as { at, kind, channel, note, velocity,
   voice }. */
function take (W)
{
    const count = W._tw_video_take();
    const p = W._tw_video_notes() / 8;
    const out = [];

    for (let i = 0; i < count; i++)
    {
        const r = W.HEAPF64.subarray(p + i * RECORD, p + (i + 1) * RECORD);

        out.push({ at: r[0], kind: r[1], channel: r[2], note: r[3], velocity: r[4],
                   voice: r[10] });
    }
    W._tw_video_clear();

    return out;
}

/* `frames' of audio, and every record taken on the way. */
function play (W, frames)
{
    const got = [];

    for (let q = 0; q * QUANTUM < frames; q++)
    {
        W._tw_render(QUANTUM);
        W._tw_events_clear();
        W._tw_keys_clear();
        if (q % EVERY === EVERY - 1)
            got.push(...take(W));
    }

    return got.concat(take(W));
}

const sine = (hz) => `node ionode { channels = 1; play = 1; out0 = o->out; };
                      node o osc::simple { freq = ${hz}; waveform = 0; amp = 1; };
                      io ionode;`;

const load = (W, channel, text) =>
    W.ccall('tw_load', 'number', ['number', 'string'], [channel, text]);

/* One C4 a second, half a second long, on `instrument'. */
function grid (W, instrument)
{
    W.ccall('tw_instrument', 'number', ['string', 'string'], ['sine.dsp', sine(100)]);

    return W.ccall('tw_piece_load', 'number', ['string', 'number'], [
        'tempo 60;\n@warmth = 0.25;\n@air = 0.75;\n' + instrument +
        'chain notes { stage g gen::grid { notes = "C4"; steps = 1; rows = 1;\n' +
        '    cells = "x"; period = 1 beats; hold = 0.5 beats; };\n' +
        '    sink { instrument = a; }; };\n', 1]) !== 0;
}

const said = (list) => JSON.stringify(list.map((r) => [r.at, r.kind, r.channel, r.note]));

/* A resend: every channel let go of, at `at'. */
const flushes = (list, at) =>
    list.filter((r) => r.kind === FLUSH).map((r) => `${r.at}:${r.channel}`).join() ===
    [...Array(16).keys()].map((c) => `${at}:${c}`).join();

const struck = (list) => list.filter((r) => r.kind !== FLUSH);

/* Whether `got' is `want', a [seconds, kind] each from `origin', every one
   at the top of the window that holds it, `late' frames later. */
function inWindows (got, want, origin, channel, late = 0)
{
    return got.length === want.length && got.every((r, k) =>
    {
        const at = origin + want[k][0] * RATE;
        const top = r.at - late;

        return r.kind === want[k][1] && r.channel === channel && r.note === 60 &&
               top % WINDOW === 0 && top <= at && at <= top + WINDOW;
    });
}

{
    const W = await worklet();
    const loaded = grid(W, 'instrument a { dsp "sine.dsp"; };\n');
    const channel = W._tw_instrument_channel(0);

    W._tw_transport(0, 0, 0);

    const got = play(W, 2.2 * RATE).filter((r) => r.kind !== FLUSH);
    const want = [[0, ON], [0.5, OFF], [1, ON], [1.5, OFF], [2, ON]];

    check("the piece's notes go to the picture at the windows they sound in",
          loaded && inWindows(got, want, W._tw_origin(), channel), `${loaded} ${said(got)}`);
}

/* A key, straight onto a channel: at the top of the window it lands in, as
   the synth sounds it. */
{
    const W = await worklet();

    load(W, 2, sine(100));
    W._tw_note_on(1000, 2, 64, 90);
    W._tw_note_off(3000, 2, 64);

    const got = play(W, 4096);
    const want = [[768, ON, 2, 64], [2816, OFF, 2, 64]];

    check('a key goes to the picture at the window it sounds in',
          said(got) === JSON.stringify(want) && got[0].velocity === 90, said(got));
}

/* All off half way through a window, with a key down: at the next window,
   which is where the synth applies it, and nothing is down after. */
{
    const W = await worklet();

    load(W, 2, sine(100));
    W._tw_note_on(0, 2, 60, 100);
    play(W, 1152);
    W._tw_all_off();

    const got = take(W);

    W._tw_video(1, 0);

    const after = struck(take(W));

    check('all off lets go of every channel at the window it applies in',
          said(got) === JSON.stringify([[1280, FLUSH, -1, 0]]) && after.length === 0,
          `${said(got)}, then ${said(after)}`);
}

/* Video turned on with a key already down, and one that came and went. */
{
    const W = await worklet();

    W._tw_video(0, 0);
    load(W, 2, sine(100));
    W._tw_note_on(0, 2, 60, 100);
    W._tw_note_on(0, 2, 62, 100);
    W._tw_note_off(512, 2, 62);
    play(W, 1024);
    W._tw_video(1, 0);

    const got = take(W);

    check('turned on, it lets go of everything and says which keys are down',
          flushes(got, 1024) && said(struck(got)) === JSON.stringify([[1024, ON, 2, 60]]),
          said(got));
}

/* A late joiner's catch-up steps the piece from a zero three seconds gone,
   silently: none of the notes in it draw. */
{
    const W = await worklet();

    grid(W, 'instrument a { dsp "sine.dsp"; };\n');
    play(W, 3 * RATE);
    W._tw_begin(0, 0, 1);

    const ons = play(W, 1.6 * RATE).filter((r) => r.kind === ON);

    check("a late joiner's catch-up draws none of the run it steps over",
          inWindows(ons, [[4, ON]], 0, W._tw_instrument_channel(0)), said(ons));
}

/* A key pressed while a seek steps silently, a window at a time. */
{
    const W = await worklet();

    grid(W, 'instrument a { dsp "sine.dsp"; };\n');
    load(W, 2, sine(100));
    W._tw_transport(0, 0, 0);
    play(W, 1024);
    W._tw_note_on(-1, 2, 62, 100);

    const before = play(W, 1024).filter((r) => r.kind === ON && r.channel === 2);

    W._tw_catch_budget(0);
    W._tw_transport(-1, TW_SEEK, 300);
    play(W, 1024);
    W._tw_note_on(-1, 2, 64, 100);

    const got = play(W, 1024).filter((r) => r.kind === ON);

    check('a key pressed while a seek is silent does not draw',
          before.length === 1 && got.length === 0, `${said(before)} then ${said(got)}`);
}

/* What the sound says: a 100 Hz sine on channel 1. */
{
    const W = await worklet();

    load(W, 1, sine(100));
    W._tw_note_on(0, 1, 60, 127);
    play(W, 8192);

    const channels = W._tw_video_inputs();
    const q = W._tw_video_input_buffer() / 4;
    const [level, bass, mid, high, ...levels] =
        W.HEAPF32.slice(q, q + 4 + channels);

    check('what the sound says: a low sine lights the bass and its channel',
          level > 0.05 && bass > 0.05 && mid < bass / 20 && high < bass / 20 &&
          channels >= 2 && levels[1] > 0.05 && levels[0] === 0,
          `level ${level} bass ${bass} mid ${mid} high ${high} levels ${levels}`);
}

/* Which channels draw, and the piece's knobs: names when they change,
   values every batch. */
{
    const W = await worklet();
    const names = () =>
    {
        const p = W._tw_video_knob_names();

        return p === 0 ? null : W.UTF8ToString(p);
    };

    load(W, 3, sine(100));

    const loose = W._tw_video_channels();
    const none = names();

    grid(W, 'instrument a { dsp "sine.dsp"; };\n');

    const channel = W._tw_instrument_channel(0);
    const both = W._tw_video_channels();
    const first = names(), again = names();
    const channels = W._tw_video_inputs();
    const q = W._tw_video_input_buffer() / 4 + 4 + channels;
    const values = [...W.HEAPF32.slice(q, q + 2)];

    check('the channels with something on them draw',
          loose === 1 << 3 && both === (1 << 3 | 1 << channel), `${loose} ${both}`);
    check("the piece's knobs, by name when they change and by value each batch",
          none === '' && first === 'air\nwarmth' && again === null &&
          values.join() === '0.75,0.25', `${none} ${first} ${again} ${values}`);

    /* More than there is room for: the first 64, said once. */
    W.ccall('tw_piece_load', 'number', ['string', 'number'],
            [Array.from({ length: 70 }, (_, k) => `@k${10 + k} = ${k};\n`).join(''), 1]);

    const many = names(), then = names();

    check('a piece with more knobs than there is room for says the first once',
          many?.split('\n').length === 64 && then === null, `${many?.length} then ${then}`);
}

/* An instrument played over MIDI out: its device hears it the page's delay
   late, and so does the picture. */
{
    const W = await worklet();

    grid(W, 'instrument a { midi "Surge"; midichannel = 3; dsp "sine.dsp"; };\n');
    W.ccall('tw_midiout_ports', 'number', ['string', 'number', 'number'],
            ['Surge XT Out', 1, 1]);
    W._tw_video(1, 50);
    W._tw_transport(0, 0, 0);

    const ons = play(W, 1.2 * RATE).filter((r) => r.kind === ON);

    check("a MIDI-out channel's notes draw its delay late",
          inWindows(ons, [[0, ON], [1, ON]], W._tw_origin(), W._tw_instrument_channel(0),
                    0.05 * RATE),
          said(ons));
}

/* A key on a device's channel while a seek is silent: the device hears it,
   so it draws. */
{
    const W = await worklet();

    grid(W, 'instrument a { midi "Surge"; midichannel = 3; dsp "sine.dsp"; };\n');
    W.ccall('tw_midiout_ports', 'number', ['string', 'number', 'number'],
            ['Surge XT Out', 1, 1]);

    const channel = W._tw_instrument_channel(0);

    W._tw_transport(0, 0, 0);
    play(W, 1024);
    W._tw_catch_budget(0);
    W._tw_transport(-1, TW_SEEK, 300);
    play(W, 1024);
    W._tw_note_on(-1, channel, 64, 100);

    const got = play(W, 1024).filter((r) => r.kind === ON);

    check("a key on a device's channel while a seek is silent draws",
          got.length === 1 && got[0].note === 64, said(got));
}

/* A channel moved off a device, drawn as the worker would: the offs still
   to come its delay late do not end what is struck on it since. */
{
    const W = await worklet();

    grid(W, 'instrument a { midi "Surge"; midichannel = 3; dsp "sine.dsp"; };\n');
    W.ccall('tw_midiout_ports', 'number', ['string', 'number', 'number'],
            ['Surge XT Out', 1, 1]);
    W._tw_video(1, 50);
    W._tw_transport(0, 0, 0);

    const got = play(W, 1.2 * RATE);

    grid(W, 'instrument a { dsp "sine.dsp"; };\n');
    W._tw_transport(W._tw_frame(), 0, 0);
    got.push(...play(W, 0.4 * RATE));

    open();
    follow(W._tw_instrument_channel(0), 'white', flat([1, 1, 1]));
    notes(got);
    M._tv_generated(1.6 * RATE);

    const frames = render();
    const at = (seconds) => frames.find((f) => f.frame === Math.round(seconds * FPS));

    check("a channel moved off a device: its late offs end nothing struck since",
          at(1.35) !== undefined && lit(at(1.35), H / 2), said(got.slice(-6)));
}

/* More in one batch than there is room for: what is down instead. */
{
    const W = await worklet();

    load(W, 2, sine(100));
    W._tw_note_on(0, 2, 40, 100);
    for (let k = 0; k < 1500; k++)
    {
        W._tw_note_on(100, 2, 50 + k % 20, 100);
        W._tw_note_off(100, 2, 50 + k % 20);
    }

    const got = play(W, 512);

    check('a batch with no room left says which keys are down instead',
          flushes(got, 512) && said(struck(got)) === JSON.stringify([[512, ON, 2, 40]]),
          said(got).slice(0, 200));
}

/* worklet.js itself, in the scope it expects, stubbed: on, with a port, it
   posts the channels and knobs once and a batch every four quanta, the
   notes and the sound in each; off, nothing. */
{
    let Processor = null;

    globalThis.sampleRate = RATE;
    globalThis.currentFrame = 0;
    globalThis.registerProcessor = (name, cls) => { Processor = cls; };
    globalThis.AudioWorkletProcessor = class
    {
        constructor () { this.port = new MessageChannel().port1; }
    };
    await import(pathToFileURL(path.join(build, 'worklet.js')).href);

    const p = new Processor();

    await p.start({ bytes: fs.readFileSync(path.join(build, 'thinkweb.wasm')),
                    windowlen: WINDOW });

    const { port1, port2 } = new MessageChannel();
    const got = [];
    const quanta = async (n) =>
    {
        const out = [[new Float32Array(QUANTUM), new Float32Array(QUANTUM)]];

        for (let k = 0; k < n; k++)
        {
            p.process([[]], out);
            globalThis.currentFrame += QUANTUM;
        }
        await new Promise((done) => setTimeout(done, 20));
    };

    port2.onmessage = (e) => got.push(e.data);
    load(p.M, 1, sine(100));
    p.M._tw_note_on(0, 1, 64, 100);
    p.receive({ type: 'video', on: true, port: port1, delayMs: 0 });
    await quanta(8);

    const batches = got.filter((m) => m.type === 'batch');
    const ons = batches.flatMap((b) => [...b.notes].filter((v, i) => i % RECORD === 1));
    const types = got.map((m) => m.type).join();

    p.receive({ type: 'video', on: false });
    got.length = 0;
    await quanta(8);
    port2.close();

    check('the worklet posts the channels, the knobs and a batch every four quanta',
          types === 'channels,knobs,batch,batch' &&
          batches.map((b) => b.frame).join() === '512,1024' &&
          ons.filter((k) => k !== FLUSH).join() === `${ON}` && batches[1].inputs[4 + 1] > 0 &&
          batches[1].knobs.length === 0 &&
          got.length === 0,
          `${types}; frames ${batches.map((b) => b.frame)}; kinds ${ons}; ` +
          `inputs ${batches[1]?.inputs}; then ${got.length} more`);
}

/* Ten seconds of a piece, drained every four quanta, with video on and with
   it off: the engine holds more as a piece warms up, and the feed holds
   nothing more than that. */
{
    const growth = [];

    for (const on of [0, 1])
    {
        const W = await worklet();

        W._tw_video(on, 0);
        grid(W, 'instrument a { dsp "sine.dsp"; };\n');
        W._tw_transport(0, 0, 0);
        play(W, RATE);

        const before = W._tw_heap_in_use();

        for (let s = 0; s < 10; s++)
        {
            play(W, RATE);
            W._tw_video_inputs();
            W._tw_video_knob_names();
        }
        growth.push(W._tw_heap_in_use() - before);
    }

    check('the feed keeps nothing on the heap as it plays', growth[1] === growth[0],
          `${growth[0]} bytes with video off, ${growth[1]} on`);
}

process.exit(failures);
