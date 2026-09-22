#!/usr/bin/env node
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
 * rollcheck.mjs -- the piano roll, through the browser module, against the
 * desktop's.
 *
 *   node wasm/web/rollcheck.mjs [BUILD_DIR] [NATIVE_BUILD_DIR]
 *
 * The roll in a browser is the desktop's RollCanvas compiled to wasm and
 * drawn in the mirror, beside the scheduler whose pending queue is the
 * future half of what it shows. The claim that makes is that there is one
 * drawing and not two, and a claim nothing checks is how the page came to
 * have a second, smaller roll in its own sixteen colours in the first
 * place.
 *
 * So the same seeded piece is played to the same instant in both builds
 * and the roll is drawn the same number of times at the same size, and
 * the two lists of ops are compared word for word. A seeded piece replays
 * identically (docs/GEN_FORMAT.md, gencheck) and the roll's drawing is a
 * function of the piece and the frame, so anything left over is the two
 * builds disagreeing about the picture.
 *
 * The native side is scripts/rollcheck, which is linked against
 * cairo-canvas2d rather than cairo for exactly this -- its `--dump' is a
 * list of ops and not pixels. It also carries the run's own numbers, and
 * this drives itself from those: one place says how long the piece is
 * played for and how big it is drawn, rather than two that could stop
 * agreeing quietly.
 *
 * Then what drawcheck.mjs asks of every other picture, of this one: the
 * list is walkable by the arity table alone, and replay.js knows every op
 * in it, at more than one size -- because a draw takes its geometry from
 * the w and h it was handed, and a roll that cached the first is a roll
 * that is wrong in every pane but the one it opened in.
 *
 * Exit status is the number of failures.
 */

import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';

import { ARITY, OP_NAMES, replay } from 'cairo-canvas2d';
import { instruments } from './piececheck.mjs';
import { loadPiece } from './render.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const top = path.join(here, '..', '..');
const build = path.resolve(process.argv[2] ?? path.join(top, 'build-web'));
const native = path.resolve(process.argv[3] ?? path.join(top, 'build'));

/* The piece. Seeded, so it replays; and one whose composers queue a phrase
   ahead rather than a note at a time, so there is a future on screen to
   have been drawn at all -- which is the half this whole roll exists for.
   scripts/rollcheck says what it does when a piece queues nothing. */
const PIECE = 'belfry.gen';

/* And the sizes the well-formedness pass draws at: a pane, and the band a
   phone in landscape leaves. */
const SIZES = [[900, 180], [360, 80]];

const BLOCK = 128;

let failures = 0;

const check = (good, what) =>
{
    process.stdout.write(`${good ? 'ok  ' : 'FAIL'}  ${what}\n`);

    if (!good)
        failures++;
};

const rollcheck = path.join(native, 'scripts', 'rollcheck');

if (!fs.existsSync(rollcheck))
{
    process.stdout.write(
        `rollcheck: no rollcheck in ${native}; build the native tree ` +
        'first -- it is the desktop half of the comparison, and it needs ' +
        "`npm ci' in wasm/web to have been run before it was configured.\n");
    process.exit(1);
}

/* ---- the desktop's list ------------------------------------------------ */

const want = JSON.parse(execFileSync(
    rollcheck,
    ['-p', path.join(native, 'plugins') + path.sep, '--dump',
     path.join(top, 'gen', PIECE)],
    { encoding: 'utf8', maxBuffer: 1 << 28,
      env: { ...process.env, THINK_DSP_PATH: path.join(top, 'dsp') } }));

/* ---- and the module's ------------------------------------------------- */

const { default: createThinkWeb } =
    await import(pathToFileURL(path.join(build, 'thinkweb.js')).href);

const dsps = instruments(build);
const gen = fs.readFileSync(path.join(build, 'gen', PIECE), 'utf8');

const { M, ok, log } = await loadPiece(createThinkWeb, {
    rate: want.rate, windowlen: want.window, block: BLOCK,
    gen, instruments: dsps,
});

if (!ok)
{
    process.stdout.write(`FAIL  ${PIECE} did not load\n      ` +
                         `${log.join('\n      ')}\n`);
    process.exit(1);
}

/* Before the transport moves, because a roll is built the moment a shell
   first says how big it is and keeps what the scheduler delivers from
   then on. One built afterwards would have an empty history and half a
   drawing, which is also what opening the pane mid-piece looks like. */
M._tw_roll_viewport(0, 0, want.width, want.height);

/* Played the way the page plays it: Play stamped for the top of the next
   window, then a block at a time, the transport stepped once per window
   inside tw_render. The native side steps to the same times by the same
   arithmetic (scripts/rollcheck, play). */
M._tw_transport(-1, 0, 0);

while (M._tw_now() < want.seconds)
{
    M._tw_render(BLOCK);
    M._tw_events_clear();
}

/* ---- the three tables, out of the heap -------------------------------- */

function readList (M)
{
    const at = M._tw_draw_ops();
    const words = M._tw_draw_words();
    const ops = words > 0
        ? M.HEAPF32.subarray(at >> 2, (at >> 2) + words)
        : new Float32Array(0);

    const strings = [];

    for (let i = 0; i < M._tw_draw_string_count(); i++)
        strings.push(M.UTF8ToString(M._tw_draw_string(i)));

    return { ops, strings };
}

/* Walked with nothing but the arity table, which is the claim the encoding
   makes. Returns the ops it counted, or a sentence. */
function walk (ops)
{
    let count = 0;

    for (let i = 0; i < ops.length; )
    {
        const op = ops[i++];

        if (OP_NAMES[op] === undefined)
            return `op ${op} at word ${i - 1} is not one`;

        let arity = ARITY[op];

        if (arity < 0)                  /* SET_DASH says its own length */
        {
            if (i >= ops.length)
                return `${OP_NAMES[op]} at word ${i - 1} has no count`;

            arity = ops[i] + 2;
        }

        if (i + arity > ops.length)
            return `${OP_NAMES[op]} at word ${i - 1} runs off the end`;

        i += arity;
        count++;
    }

    return count;
}

/* A context that draws nothing and counts what it was asked for: what is
   under test is that every op in the list is one replay.js knows, and it
   says so by throwing. drawcheck.mjs has the same stand-in. */
function counter ()
{
    const nothing = () => { ctx.calls++; };
    const ctx = {
        calls: 0,
        beginPath: nothing, moveTo: nothing, lineTo: nothing,
        bezierCurveTo: nothing, arc: nothing, rect: nothing,
        closePath: nothing, fill: nothing, stroke: nothing, clip: nothing,
        fillRect: nothing, clearRect: nothing, fillText: nothing,
        drawImage: nothing, save: nothing, restore: nothing,
        translate: nothing, scale: nothing, setTransform: nothing,
        setLineDash: nothing,
    };

    return ctx;
}

/* ---- one picture, drawn twice ----------------------------------------- */

/* The same number of frames as the native side, because the pitch range
   eases toward its fit one picture at a time: "the same frame" is the same
   count of draws as well as the same instant. */
let words = -1;

for (let i = 0; i < want.frames; i++)
    words = M._tw_roll_draw(want.width, want.height);

check(words > 0, `${PIECE} at ${want.seconds}s draws ${words} words ` +
                 `through the module`);

const got = readList(M);

if (words > 0)
{
    /* fround on the way in: the dump is %.9g, which round-trips a float
       exactly but parses here as the double nearest that decimal. The
       module's ops come out of a Float32Array and are the float itself,
       so one of the two has to be narrowed and this is the one that can
       be. */
    let first = -1;

    for (let i = 0; i < Math.max(got.ops.length, want.ops.length); i++)
        if (got.ops[i] !== Math.fround(want.ops[i]))
        {
            first = i;
            break;
        }

    check(first < 0,
          first < 0
              ? `and the desktop's ${want.ops.length} words are the same ` +
                'words, one class compiled twice'
              : `the two lists differ at word ${first}: the module says ` +
                `${got.ops[first]}, the desktop ` +
                `${Math.fround(want.ops[first])} ` +
                `(${got.ops.length} words against ${want.ops.length})`);

    const sameStrings =
        got.strings.length === want.strings.length &&
        got.strings.every((s, i) => s === want.strings[i]);

    check(sameStrings,
          sameStrings
              ? `and the ${want.strings.length} string(s) it indexes: ` +
                `${want.strings.join(', ') || 'none'}`
              : `the strings differ: [${got.strings}] against ` +
                `[${want.strings}]`);
}

/* ---- and the list is one, at more than one size ----------------------- */

for (const [w, h] of SIZES)
{
    M._tw_roll_viewport(0, 0, w, h);

    const n = M._tw_roll_draw(w, h);

    if (n <= 0)
    {
        check(false, `the roll at ${w}x${h}: drew nothing`);
        continue;
    }

    const { ops, strings } = readList(M);
    const walked = walk(ops);

    if (typeof walked === 'string')
    {
        check(false, `the roll at ${w}x${h}: ${walked}`);
        continue;
    }

    try
    {
        replay(counter(), ops, strings, [], { width: w, height: h, dpr: 1 });
        check(true, `the roll at ${w}x${h} is ${walked} ops replay.js knows`);
    }
    catch (e)
    {
        check(false, `the roll at ${w}x${h}: ${e.message}`);
    }
}

process.stdout.write(failures === 0
    ? '\nthe browser draws the desktop\'s roll, word for word\n'
    : `\n${failures} failed\n`);

process.exitCode = failures;
