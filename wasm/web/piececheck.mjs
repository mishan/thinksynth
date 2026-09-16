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
 * piececheck.mjs -- every seeded piece, composed by the browser build and
 * held against what genwav composes.
 *
 *   node wasm/web/piececheck.mjs [BUILD_DIR] [NODE_BUILD_DIR]
 *
 * M2's gate, without a browser (JAM.md, section 6). For each shipped piece
 * that pins a seed, genwav.mjs renders it under Node out of wasm/'s module
 * -- libthink and the plugins as side modules, the scheduler stepped by a
 * fixed virtual clock in windows of 1024 at 44.1 kHz -- and the browser's
 * module composes the same seconds with the scheduler stepped by the audio
 * clock in windows of 256 and of 128, at 48 kHz and at 44.1. The tapes must
 * be the same tape.
 *
 * Two gates in one, which is why it is one comparison. That two different
 * wasm builds of the same tree agree is the wasm-against-wasm gate: nothing
 * about linking every plugin in rather than dlopening it may change what a
 * piece composes. That four different step sizes agree is the
 * step-invariance gate: what a piece is must be a function of the file and
 * the seed and not of the host's buffer, or two peers with different sound
 * cards could not play the same piece (JAM.md, section 3).
 *
 * The pieces that pin no seed are skipped and named. They draw their master
 * seed from the host at load and are not meant to repeat -- there is no
 * tape for them to have.
 *
 * Then the other command, which no tape comparison can reach: a key. A
 * seeded piece composes the same whoever is listening, so nothing above
 * says whether `input midi' arrives. gen/hands.gen is the piece that is
 * nothing but input -- three chains, no generators, no instruments -- and
 * the last check here holds a chord down in it and listens.
 *
 * What this cannot see is the browser: the worklet, its messages, and the
 * quanta a real audio thread asks for. browsertest.mjs runs the same tape
 * comparison through Chromium and Firefox.
 *
 * Exit status is the number of failures.
 */

import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';

import { tapeBefore } from '../tape.mjs';
import { playAt, playPiece } from './render.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const top = path.join(here, '..', '..');
const build = path.resolve(process.argv[2] ?? path.join(top, 'build-web'));
const nodeBuild = path.resolve(process.argv[3] ??
                               process.env.THINK_WASM_BUILD ??
                               path.join(top, 'build-wasm'));

/* How much of each piece. A minute is what the step-size fix was measured
   over and is long enough for the slowest shipped piece to have said
   something; tide delivers a couple of thousand events in it. */
export const SECONDS = 60;

/* The rate and window each run uses. The first is the page's own; the rest
   are what has to agree with it. */
export const SHAPES = [
    { rate: 48000, windowlen: 256 },
    { rate: 48000, windowlen: 128 },
    { rate: 44100, windowlen: 256 },
    { rate: 44100, windowlen: 128 },
];

/* The .dsp files a piece's instruments are looked up among -- the same ones
   the page hands the worklet before it loads anything. */
export function instruments (buildDir)
{
    const dir = path.join(buildDir, 'dsp');
    const out = {};

    for (const name of JSON.parse(
             fs.readFileSync(path.join(dir, 'index.json'), 'utf8')))
        out[name] = fs.readFileSync(path.join(dir, name), 'utf8');

    return out;
}

/* Every shipped piece, and whether it pins a seed. `seed N;' at the start of
   a line is the whole of the syntax (thcGenFile.cpp). */
export function pieces (buildDir)
{
    const dir = path.join(buildDir, 'gen');

    return JSON.parse(fs.readFileSync(path.join(dir, 'index.json'), 'utf8'))
        .map((name) =>
        {
            const text = fs.readFileSync(path.join(dir, name), 'utf8');

            return { name, text, seeded: /^seed\s+\d+\s*;/m.test(text) };
        });
}

/* genwav.mjs's tape for a piece, cut to SECONDS. The reference: a different
   module, a different loader, a different step. */
export function reference (name, nodeBuildDir)
{
    const tape = path.join(nodeBuildDir, `${name}.tape`);

    execFileSync('node',
                 [path.join(here, '..', 'genwav.mjs'),
                  '-s', String(SECONDS), '-t', tape, '-q',
                  path.join(top, 'gen', name)],
                 { cwd: top, env: { ...process.env,
                                    THINK_WASM_BUILD: nodeBuildDir } });

    const text = fs.readFileSync(tape, 'utf8');

    fs.rmSync(tape, { force: true });

    return tapeBefore(text, SECONDS);
}

/* Where the two first differ, for a failure that can be acted on. */
export function firstDifference (want, got)
{
    const a = want.split('\n'), b = got.split('\n');
    let i = 0;

    while (i < a.length && i < b.length && a[i] === b[i])
        i++;

    return `line ${i + 1}: genwav has ${a[i] ? `[${a[i]}]` : 'nothing'}, ` +
           `this has ${b[i] ? `[${b[i]}]` : 'nothing'}` +
           ` (${a.length - 1} events against ${b.length - 1})`;
}

/* A chord held down in gen/hands.gen.
 *
 * Its first chain is `input midi' into a gen::arp with `pass = 0': the
 * chord it is handed is eaten, and what comes out is a figure walking up
 * through the held notes and their octave, one step every `period'. So a
 * press that arrives produces notes that are *not* the notes pressed, which
 * is what makes this worth asserting -- a chord passed straight through
 * would look like success and mean the arp never saw it.
 *
 * The piece declares no instruments, so a patch goes on the channel first;
 * without one the figure is composed and nothing sounds.
 */
async function checkKeys (createThinkWeb, dsps, all)
{
    const piece = all.find((p) => p.name === 'hands.gen');

    if (piece === undefined)
    {
        process.stdout.write('FAIL  hands.gen is not in this build\n');
        return 1;
    }

    const CHANNEL = 0;              /* the engine's: the file writes 1 */
    const CHORD = [53, 56, 60];     /* F3 Ab3 C4, and hands.gen is F minor */
    const HELD = 4;

    const r = await playAt(createThinkWeb, {
        gen: piece.text,
        instruments: dsps,
        patches: { [CHANNEL]: dsps['rpiano0.dsp'] },
        keys: [
            ...CHORD.map((note) => ({ at: 1, channel: CHANNEL, note,
                                      velocity: 100 })),
            ...CHORD.map((note) => ({ at: 1 + HELD, channel: CHANNEL, note })),
        ],
        seconds: 1 + HELD + 1,
    });

    if (!r.ok)
    {
        process.stdout.write(`FAIL  hands.gen did not load: ` +
                             `${r.errors.join('; ')}\n`);
        return 1;
    }

    const notes = r.events.filter((e) => e.kind === 'N' &&
                                         e.channel === CHANNEL);

    /* Every step of the figure is a held note or one of them an octave up:
       `octaves = 2' in the piece. */
    const reachable = new Set([...CHORD, ...CHORD.map((n) => n + 12)]);
    const stray = notes.filter((e) => !reachable.has(e.note));

    /* One step every `period', which the piece sets to the Step knob's
       default of 0.12 s. Held for HELD seconds, so: about thirty. Asserted
       loosely, because the exact count is a function of where the press
       landed in a window and the figure is the point, not the arithmetic. */
    const want = HELD / 0.12;
    const complaints = [];

    if (notes.length < want * 0.8 || notes.length > want * 1.2)
        complaints.push(`${notes.length} steps in ${HELD} s of held chord, ` +
                        `not about ${Math.round(want)}`);

    if (stray.length > 0)
        complaints.push(`${stray.length} steps on notes nobody held ` +
                        `(${stray.slice(0, 4).map((e) => e.note).join(', ')})`);

    /* The chord itself must not be among them: `pass = 0' eats it, and an
       arp that passed the press through would be an arp that never held
       it. The figure's steps carry a duration; a passed-through press does
       not. */
    if (notes.some((e) => e.duration === 0))
        complaints.push('a press was passed straight through; ' +
                        'the arp did not eat it');

    if (r.peak === 0)
        complaints.push('the figure was composed but nothing sounded');

    if (complaints.length > 0)
    {
        process.stdout.write(`FAIL  hands.gen     a held chord: ` +
                             `${complaints.join('; ')}\n`);
        return 1;
    }

    process.stdout.write(
        `ok    hands.gen      a chord held ${HELD} s became ` +
        `${notes.length} steps, peak ${r.peak.toFixed(3)}\n`);

    return 0;
}

if (import.meta.url === pathToFileURL(process.argv[1]).href)
{
    if (!fs.existsSync(path.join(nodeBuild, 'thinksynth.mjs')))
    {
        process.stdout.write(
            `piececheck: no Node module in ${nodeBuild}. It is the tape ` +
            'everything here is compared against;\n            build it ' +
            'first -- see the top of wasm/CMakeLists.txt.\n');
        process.exit(1);
    }

    const { default: createThinkWeb } =
        await import(pathToFileURL(path.join(build, 'thinkweb.mjs')).href);

    const dsps = instruments(build);
    const all = pieces(build);
    let failures = 0;

    for (const piece of all)
    {
        if (!piece.seeded)
        {
            process.stdout.write(`skip  ${piece.name.padEnd(14)} pins no ` +
                                 'seed; it is not meant to repeat\n');
            continue;
        }

        const want = reference(piece.name, nodeBuild);
        const cells = [];

        for (const shape of SHAPES)
        {
            const r = await playPiece(createThinkWeb,
                                      { ...shape, gen: piece.text,
                                        instruments: dsps,
                                        seconds: SECONDS });
            const at = `${shape.rate / 1000}k/${shape.windowlen}`;

            if (!r.ok)
            {
                failures++;
                cells.push(`${at} did not load: ${r.errors.join('; ')}`);
                continue;
            }

            const got = tapeBefore(r.tape, SECONDS);

            if (got === want)
                cells.push(`${at} ok`);
            else
            {
                failures++;
                cells.push(`${at} DIFFERS -- ${firstDifference(want, got)}`);
            }
        }

        const bad = cells.some((c) => !c.endsWith('ok'));

        process.stdout.write(
            `${bad ? 'FAIL' : 'ok  '}  ${piece.name.padEnd(14)} ` +
            `${String(want.split('\n').length - 1).padStart(5)} events   ` +
            `${cells.join('   ')}\n`);
    }

    failures += await checkKeys(createThinkWeb, dsps, all);

    process.stdout.write(
        `\n${failures === 0
             ? 'every seeded piece composes the same tape in the browser ' +
               'build as in genwav, at every step, and a chord held in ' +
               'hands.gen is arpeggiated\n'
             : `${failures} failed\n`}`);
    process.exitCode = failures;
}
