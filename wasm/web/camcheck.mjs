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
 * camcheck.mjs -- the camera's controller through the browser build, from
 * Node.
 *
 *   node wasm/web/camcheck.mjs [BUILD_DIR]
 *
 * What camworker.js asks of the module (twcamera.cpp), with drawn frames
 * in place of a camera: a square flashed into a zone of the picture
 * strikes that zone's note in the camera's own profile and lets it go once
 * the picture is still, on the zone the player sees, mirrored, and says
 * the zone struck for the camera view; a camera gone lets go of what it
 * holds. hid/thhCamera.h's thresholds and the
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

/* The zones struck, each with how hard, as the frames fed since this was
   last emptied said them, and what the controls read at the last. */
const struck = [], downs = [];
let values = [];

/* Feeds frames 1/120 s apart from `t'; the notes they played, in order. */
function feed (frames, t = 0)
{
    const notes = [];

    struck.length = downs.length = 0;
    frames.forEach((luma, i) =>
    {
        const at = M._tc_luma(W, H);

        M.HEAPU8.set(luma, at);

        const said = JSON.parse(M.UTF8ToString(M._tc_feed(W, H, t + i / 120)));

        notes.push(...said.notes);
        struck.push(...(said.hits ?? []));
        downs.push(said.down);
        values = said.values;
    });

    return notes;
}

const gone = () => JSON.parse(M.UTF8ToString(M._tc_gone())).notes;

check('the camera\'s own profile binds',
      M.ccall('tc_start', 'string', ['string'], ['']) === '');

/* The player's bottom left is the camera's bottom right: zone 7, the
   profile's lowest note. Struck, then held still. */
const still = Array(40).fill(frame([5 / 6, 5 / 6]));
let notes = feed([...Array(10).fill(frame(null)), ...still]);

check('a square flashed into the player\'s bottom left strikes C4, then lets it go',
      notes.length === 2 && notes[0][0] === 60 && notes[0][1] >= 40 &&
      notes[1][0] === 60 && notes[1][1] === 0,
      JSON.stringify(notes));
check('and says zone 7 struck, once, with how hard',
      struck.length === 1 && struck[0][0] === 6 && struck[0][1] > 0 && struck[0][1] <= 1,
      JSON.stringify(struck));
check('and that it can\'t strike again till it is still',
      downs.includes(1 << 6) && downs.at(-1) === 0, JSON.stringify(downs));

/* The player's top right, the camera's top left: zone 3, the highest. */
notes = feed([...Array(10).fill(frame(null)), ...Array(5).fill(frame([1 / 6, 1 / 6]))], 1);
check('a square flashed into the player\'s top right strikes G5, and a camera gone lets it go',
      notes.length === 1 && notes[0][0] === 79 && notes[0][1] > 0 &&
      JSON.stringify(gone()) === '[[79,0]]',
      JSON.stringify(notes));
check('and says zone 3 struck', struck.length === 1 && struck[0][0] === 2,
      JSON.stringify(struck));

notes = feed([...Array(10).fill(frame(null)), ...still], 2);
check('after a camera gone, the next frames start afresh',
      notes.length === 2 && notes[0][0] === 60 && notes[1][1] === 0,
      JSON.stringify(notes));
check('and says what each of its 10 buttons and 15 axes reads',
      values.length === 25 && values.every((v) => typeof v === 'number'), JSON.stringify(values));

/* The player's profile over its own. */
check('the player\'s profile binds over its own',
      M.ccall('tc_start', 'string', ['string'], ['device camera { hit_7 = note 5; };']) === '');
notes = feed([...Array(10).fill(frame(null)), ...still], 3);
check('and zone 7 plays the player\'s F4', notes[0]?.[0] === 65, JSON.stringify(notes));

const changed = JSON.parse(M.ccall('tc_profile', 'string', ['string'],
                                   ['device camera { hit_7 = note 7; };']));

notes = feed([...Array(10).fill(frame(null)), ...still], 4);
check('changed as it plays, zone 7 plays G4, and nothing is said wrong',
      changed.problem === '' && notes[0]?.[0] === 67, JSON.stringify([changed, notes]));

const wrong = M.ccall('tc_start', 'string', ['string'], ['device camera { nonsense = note 0; };']);

notes = feed([...Array(10).fill(frame(null)), ...still], 5);
check('one that doesn\'t fit is said, and its own plays',
      /nonsense/.test(wrong) && notes[0]?.[0] === 60, JSON.stringify([wrong, notes]));

/* A bend on the camera is the theremin's to play, not a note. */
M.ccall('tc_start', 'string', ['string'], ['device camera { x = bend { range = 2; }; };']);
notes = feed([...Array(10).fill(frame(null)), ...still.map((f, i) => frame([5 / 6 - i / 200, 5 / 6]))], 6);
check('a bend it plays is no note', notes.length === 2, JSON.stringify(notes));

process.exit(failures);
