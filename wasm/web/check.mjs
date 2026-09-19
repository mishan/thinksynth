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
 * check.mjs -- the browser build, played from Node.
 *
 *   node wasm/web/check.mjs [BUILD_DIR]
 *
 * Every .dsp the page offers is loaded into the module the page runs, and a
 * note is played through it the way the worklet plays one: stamped, a
 * 128-frame block at a time, at 48 kHz in windows of 256. Each must load,
 * make sound, make nothing that is not a number, and make the same sound
 * twice. Then one stamp is checked for where it lands -- in the window its
 * frame falls in, not before and not a window late.
 *
 * What this cannot see is the browser: the worklet, its messages, and the
 * quanta a real audio thread asks for. browsertest.mjs holds that against
 * this, to the bit.
 *
 * Exit status is the number of failures.
 */

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

import { renderDirect } from './render.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const build = path.resolve(process.argv[2] ??
                           path.join(here, '..', '..', 'build-web'));

const { default: createThinkWeb } =
    await import(pathToFileURL(path.join(build, 'thinkweb.js')).href);

const RATE = 48000;
const FRAMES = RATE;           /* one second */
const NOTE = { note: 60, velocity: 100 };

let failures = 0;

const fail = (what) =>
{
    process.stdout.write(`FAIL  ${what}\n`);
    failures++;
};

const dspDir = path.join(build, 'dsp');
const names = JSON.parse(fs.readFileSync(path.join(dspDir, 'index.json'),
                                         'utf8'));

for (const name of names)
{
    /* An effect graph has no note to play and is not an instrument; the
       index carries it so the worklet can be handed it. fxcheck is its
       gate. */
    if (name.startsWith('fx/'))
    {
        process.stdout.write(`skip  ${name}: an effect graph\n`);
        continue;
    }

    /* And the kit is not a graph at all -- it is the wavs osc::sample
       plays, carried in the same index because the worklet has to be
       handed them and cannot fetch. statecheck is their gate. */
    if (name.startsWith('samples/'))
    {
        process.stdout.write(`skip  ${name}: a sample, not a graph\n`);
        continue;
    }

    const text = fs.readFileSync(path.join(dspDir, name), 'utf8');
    const events = [
        { on: true, frame: 0, ...NOTE },
        { on: false, frame: RATE / 2, note: NOTE.note },
    ];

    const a = await renderDirect(createThinkWeb,
                                 { rate: RATE, text, events, frames: FRAMES });

    if (!a.ok)
    {
        fail(`${name}: did not load\n      ${a.log.join('\n      ')}`);
        continue;
    }

    let peak = 0, bad = 0;

    for (const v of a.out)
    {
        if (!Number.isFinite(v))
            bad++;
        else
            peak = Math.max(peak, Math.abs(v));
    }

    const b = await renderDirect(createThinkWeb,
                                 { rate: RATE, text, events, frames: FRAMES });
    const same = Buffer.from(a.out.buffer).equals(Buffer.from(b.out.buffer));

    if (bad > 0)
        fail(`${name}: ${bad} samples that are not numbers`);
    else if (peak === 0)
        fail(`${name}: silent`);
    else if (!same)
        fail(`${name}: two renders differ`);
    else
        process.stdout.write(`ok    ${name.padEnd(14)} peak ${peak.toFixed(3)}\n`);
}

/* A note stamped at frame 1000 is applied in the window 768-1023, and a note
   sounds from the window after the one it is added in -- the engine's own
   onset, the desktop's too. So: nothing before 1024, something before 1280.
   A stamp a window late or early moves that by a window either way. */
{
    const text = fs.readFileSync(path.join(dspDir, 'ts1.dsp'), 'utf8');
    const r = await renderDirect(createThinkWeb, {
        rate: RATE, text, frames: 4096,
        events: [ { on: true, frame: 1000, ...NOTE } ],
    });

    let first = -1;

    for (let i = 0; i < r.out.length; i++)
        if (r.out[i] !== 0) { first = i >> 1; break; }

    const applied = Math.floor(1000 / r.windowlen) * r.windowlen;
    const heard = applied + r.windowlen;

    if (first < heard || first >= heard + r.windowlen)
        fail(`stamp: a note at frame 1000 first sounded at ${first}, not in ` +
             `the window ${heard}-${heard + r.windowlen - 1}`);
    else
        process.stdout.write(`ok    stamp          frame 1000 -> applied at ` +
                             `${applied}, first sound at ${first}\n`);
}

process.stdout.write(`\n${failures === 0 ? 'all passed' : failures + ' failed'}\n`);
process.exitCode = failures;
