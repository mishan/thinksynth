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
 * handscheck.mjs -- two hands played as a theremin through the browser
 * build, from Node.
 *
 *   node wasm/web/handscheck.mjs [BUILD_DIR]
 *
 * What camworker.js asks of the module as a theremin (twhands.cpp), with
 * hands made up in place of a tracker's: the left hand pinched plays C3,
 * the right hand's height bends it to a scale's step, or freely unsnapped,
 * how far the left hand is below the top hushes it, and a hand let go of,
 * or a camera gone, lets it go. Which hand is which and the profile itself
 * are held natively by scripts/hidhandcheck; this is the way in.
 *
 * Exit status is the number of failures.
 */

import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const build = path.resolve(process.argv[2] ?? path.join(here, '..', '..', 'build-web'));
const { default: createThinkWeb } =
    await import(pathToFileURL(path.join(build, 'thinkweb.js')).href);
const M = await createThinkWeb({ print: () => {}, printErr: () => {} });

let failures = 0;

function check (name, ok, why = '')
{
    console.log(`${ok ? 'ok  ' : 'FAIL'}  ${name}${ok ? '' : `\n  ${why}`}`);
    failures += ok ? 0 : 1;
}

/* A hand's 44 floats (twhands.cpp): its palm at x, y as the player sees
   it, a tenth of the picture high, pinched or open, and no word from the
   tracker on which it is. */
function hand (x, y, pinched)
{
    const f = new Float32Array(44);
    const cx = 1 - x, cy = 1 - y, s = 0.1;

    f.fill(cx, 0, 21);
    f.fill(cy - 0.3 * s, 21, 42);
    f[21] = cy + 0.4 * s;
    for (let k = 0; k < 4; k++)
    {
        f[5 + 4 * k] = cx + (k - 1.5) * 0.1 * s;
        f[21 + 5 + 4 * k] = cy - 0.1 * s;
    }
    f[4] = cx - (pinched ? 0 : 0.06);
    f[8] = cx + (pinched ? 0 : 0.06);
    f[42] = -1;
    return f;
}

/* Frames a tenth of a second apart from `t', each a list of hands; what
   they said, run together. */
function feed (frames, t = 0)
{
    const out = { notes: [], bends: [], aux: [], last: null };

    frames.forEach((hands, i) =>
    {
        const at = M._hd_hands(hands.length);

        hands.forEach((h, n) => M.HEAPF32.set(h, (at >> 2) + n * 44));

        const said = JSON.parse(M.UTF8ToString(M._hd_feed(hands.length, t + i / 10)));

        out.notes.push(...said.notes);
        out.bends.push(...said.bends);
        out.aux.push(...said.aux);
        out.last = said;
    });
    return out;
}

const gone = () => JSON.parse(M.UTF8ToString(M._hd_gone()));
const near = (a, b) => Math.abs(a - b) < 0.02;

check('the hands\' own profile binds', M.ccall('hd_start', 'string', ['number'], [1]) === '');

/* The right hand a fifth of the way up, the left pinched halfway up, then
   the left let go of. */
const right = hand(0.7, 0.2, false);
let got = feed([...Array(8).fill([right, hand(0.3, 0.8, true)]), ...Array(4).fill([right])]);

check('a left pinch plays C3, and letting the hand go ends it',
      JSON.stringify(got.notes.map(([n, v]) => [n, v > 0])) === '[[48,true],[48,false]]',
      JSON.stringify(got.notes));
check('the right hand a fifth of the way up bends it to the scale\'s step there, 7',
      near(got.bends.at(-1)?.[1], 7), JSON.stringify(got.bends.slice(-2)));
check('the left hand 0.8 up hushes it by 0.2, on aux3',
      got.aux.at(-1)?.[1] === 3 && near(got.aux.at(-1)?.[2], 0.2), JSON.stringify(got.aux.slice(-2)));
check('and where each hand is is said, the right one still there',
      got.last.left === null && near(got.last.right[0], 0.7) && near(got.last.right[1], 0.2),
      JSON.stringify([got.last.left, got.last.right]));

feed([...Array(4).fill([hand(0.3, 0.5, true)])], 5);

const left = gone().notes;

check('a camera gone lets go of what it holds', JSON.stringify(left) === '[[48,0]]',
      JSON.stringify(left));
check('the unsnapped profile binds', M.ccall('hd_start', 'string', ['number'], [0]) === '');
got = feed([...Array(8).fill([hand(0.7, 0.21, false), hand(0.3, 0.5, true)])], 10);
check('unsnapped, the bend goes where the hand is: 0.21 of 36 semitones',
      near(got.bends.at(-1)?.[1], 0.21 * 36), JSON.stringify(got.bends.slice(-2)));

process.exit(failures);
