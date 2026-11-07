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
 * ear.js -- the module again, in a worker, hearing for the others.
 *

/*
 * camcheck.mjs -- the camera's controller through the browser build, from
 * Node.
 *
 *   node wasm/web/camcheck.mjs [BUILD_DIR]
 *
 * What camworker.js asks of the module (twcamera.cpp), with drawn frames
 * in place of a camera: a square flashed into a zone of the picture
 * strikes that zone's note in the camera's own profile and lets it go once
 * the picture is still, on the zone the player sees, mirrored; a camera
 * gone lets go of what it holds. hid/thhCamera.h's thresholds and the
 * mapper are held natively by scripts/hidcamcheck; this is the way in.
 *
 * Exit status is the number of failures.
 */

import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const build = path.resolve(process.argv[2] ??
                           path.join(here, '..', '..', 'build-web'));

const { default: createThinkWeb } =
    await import(pathToFileURL(path.join(build, 'thinkweb.js')).href);

const M = await createThinkWeb({ print: () => {}, printErr: () => {} });

let failures = 0;

function check (name, ok, why = '')
{
    console.log(`${ok ? 'ok  ' : 'FAIL'}  ${name}${ok ? '' : `\n  ${why}`}`);
    failures += ok ? 0 : 1;
}

const W = 160, H = 120;

/* A dark picture, with a bright square on it where `at' says: its center
   as fractions of the camera's picture, which is the player's mirrored. */
function frame (at)
{
    const luma = new Uint8Array(W * H).fill(40);

    if (at)
        for (let y = Math.round(at[1] * H) - 15; y < Math.round(at[1] * H) + 15; y++)
            for (let x = Math.round(at[0] * W) - 15; x < Math.round(at[0] * W) + 15; x++)
                luma[y * W + x] = 220;

    return luma;
}

/* Feeds frames 1/120 s apart from `t'; the notes they played, in order. */
function feed (frames, t = 0)
{
    const notes = [];

    frames.forEach((luma, i) =>
    {
        M.HEAPU8.set(luma, M._tc_luma(W, H));
        notes.push(...JSON.parse(M.UTF8ToString(
            M._tc_feed(W, H, t + i / 120))).notes);
    });

    return notes;
}

const gone = () => JSON.parse(M.UTF8ToString(M._tc_gone())).notes;

check('the camera\'s own profile binds',
      M.ccall('tc_start', 'string', [], []) === '');

/* The player's bottom left is the camera's bottom right: zone 7, the
   profile's lowest note. Struck, then held still. */
const still = Array(40).fill(frame([5 / 6, 5 / 6]));
let notes = feed([...Array(10).fill(frame(null)), ...still]);

check('a square flashed into the player\'s bottom left strikes C4, then lets it go',
      notes.length === 2 && notes[0][0] === 60 && notes[0][1] >= 40 &&
      notes[1][0] === 60 && notes[1][1] === 0,
      JSON.stringify(notes));

/* The player's top right, the camera's top left: zone 3, the highest. */
notes = feed([...Array(10).fill(frame(null)), ...Array(5).fill(frame([1 / 6, 1 / 6]))], 1);
check('a square flashed into the player\'s top right strikes G5, and a camera gone lets it go',
      notes.length === 1 && notes[0][0] === 79 && notes[0][1] > 0 &&
      JSON.stringify(gone()) === '[[79,0]]',
      JSON.stringify(notes));

notes = feed([...Array(10).fill(frame(null)), ...still], 2);
check('after a camera gone, the next frames start afresh',
      notes.length === 2 && notes[0][0] === 60 && notes[1][1] === 0,
      JSON.stringify(notes));

process.exit(failures);
