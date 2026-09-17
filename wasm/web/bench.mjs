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
 * bench.mjs -- what one quantum costs with a piece running.
 *
 *   node wasm/web/bench.mjs [BUILD_DIR]
 *
 * The measurement JAM.md's M2 names as the one that could send the
 * scheduler back out of the worklet. A quantum is 128 frames, 2.67 ms at
 * 48 kHz, and it is a deadline: whatever happens inside process() has to
 * finish inside that or the page hears it. What happens inside it here is
 * the synth's render, the scheduler's step once per window, every note that
 * step delivered being built into the graph, and -- since the player is
 * still playing -- a chord's worth of note-ons on top.
 *
 * So each piece is run twice: as it is, and with a six-note chord pressed
 * and released over and over, which is the same thing a key costs and the
 * thing that allocates (thinkweb.cpp, applyDue). What is reported is the
 * distribution of the per-quantum time and the worst one, against the
 * budget.
 *
 * Node rather than a browser on purpose: this is the engine's cost, and
 * the engine is the same wasm either way. What a browser adds on top is the
 * port and the de-interleave, and browsertest.mjs already holds a browser's
 * render against this module's to the sample. What a browser does *not*
 * add, and this is the point of the placement, is a timer, a lookahead or
 * anything a background tab can throttle.
 *
 * The number that decides anything is this run on the slowest machine the
 * thing is meant to play on, not on the one it was written on.
 */

import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

import { instruments, pieces } from './piececheck.mjs';
import { loadPiece } from './render.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const build = path.resolve(process.argv[2] ??
                           path.join(here, '..', '..', 'build-web'));

const { default: createThinkWeb } =
    await import(pathToFileURL(path.join(build, 'thinkweb.js')).href);

const RATE = 48000;
const WINDOW = 256;
const QUANTUM = 128;
const SECONDS = 60;

/* A chord, pressed and released twice a second: six voices is a hand. */
const CHORD = [48, 55, 60, 64, 67, 72];
const CHORD_EVERY = 0.5;

function quantile (sorted, q)
{
    return sorted[Math.min(sorted.length - 1,
                           Math.floor(q * sorted.length))];
}

const ms = (x) => `${x.toFixed(3)} ms`;

async function run (gen, dsps, chord)
{
    const { M, ok, errors } =
        await loadPiece(createThinkWeb, { rate: RATE, windowlen: WINDOW,
                                          block: QUANTUM, gen,
                                          instruments: dsps });

    if (!ok)
        return { errors };

    M._tw_transport(-1, 0, 0);

    /* Allocated once, and a typed array: a JavaScript array grown a
       measurement at a time collects garbage inside the thing it is
       measuring, and this is a measurement whose interesting number is the
       single worst sample. */
    const times = new Float64Array(
        Math.ceil(SECONDS * RATE / QUANTUM) + 2);
    let taken = 0;
    let notes = 0;
    let nextChord = 0;
    let down = false;
    let worst = 0, worstAt = 0;
    let moving = 0;             /* the worst after the first quantum */

    while (M._tw_now() < SECONDS)
    {
        if (chord && M._tw_now() >= nextChord)
        {
            for (const n of CHORD)
            {
                if (down)
                    M._tw_note_off(-1, 0, n);
                else
                {
                    M._tw_note_on(-1, 0, n, 100);
                    notes++;
                }
            }

            down = !down;
            nextChord += CHORD_EVERY;
        }

        const at = M._tw_now();
        const t0 = process.hrtime.bigint();

        M._tw_render(QUANTUM);

        const took = Number(process.hrtime.bigint() - t0) / 1e6;

        if (took > worst)
        {
            worst = took;
            worstAt = at;
        }

        if (at > 0 && took > moving)
            moving = took;

        times[taken++] = took;
    }

    return { times: times.subarray(0, taken).sort(), notes, worst, worstAt,
             moving };
}

const budget = QUANTUM / RATE * 1000;
const dsps = instruments(build);

process.stdout.write(
    `a quantum of ${QUANTUM} frames at ${RATE} Hz is ${budget.toFixed(2)} ms, ` +
    `and that is the deadline\n` +
    `the synth runs in windows of ${WINDOW}, so one quantum in ` +
    `${WINDOW / QUANTUM} carries a scheduler step\n\n` +
    `piece            chord        p50       p99     p99.9       max    ` +
    `of quantum   worst at\n`);

const rows = [];

for (const piece of pieces(build))
{
    if (!piece.seeded)
        continue;

    for (const chord of [false, true])
    {
        const r = await run(piece.text, dsps, chord);

        if (r.errors !== undefined)
        {
            process.stdout.write(`${piece.name}: did not load -- ` +
                                 `${r.errors.join('; ')}\n`);
            continue;
        }

        rows.push({ name: piece.name, chord, ...r });

        process.stdout.write(
            `${piece.name.padEnd(15)} ` +
            `${(chord ? `${r.notes} notes` : 'none').padEnd(10)} ` +
            `${ms(quantile(r.times, 0.5))} ` +
            `${ms(quantile(r.times, 0.99))} ` +
            `${ms(quantile(r.times, 0.999))} ` +
            `${ms(r.worst)} ` +
            `${(r.worst / budget * 100).toFixed(1).padStart(8)}%   ` +
            `${r.worstAt.toFixed(2)} s\n`);
    }
}

rows.sort((a, b) => b.worst - a.worst);

const top = rows[0];

/* The first quantum of a run is the one that builds every instrument's
   graph, and the transport has not moved when it does. Told apart from the
   rest because it is the one expensive step nobody can hear: it lands
   before there is any audio for it to interrupt. Each run's worst *after*
   that quantum, rather than the runs whose worst happened not to be it --
   on a machine where every first quantum is the worst there would be no
   such run, and on any other a run's second-worst was being thrown away
   with its first. */
const running = [...rows].sort((a, b) => b.moving - a.moving)[0];

process.stdout.write(
    `\nworst of all: ${top.name} at ${top.worst.toFixed(3)} ms, ` +
    `${(top.worst / budget * 100).toFixed(1)}% of the quantum, ` +
    `${top.worstAt.toFixed(2)} s in\n` +
    `worst once the transport is moving: ${running.name} at ` +
    `${running.moving.toFixed(3)} ms, ` +
    `${(running.moving / budget * 100).toFixed(1)}% of the quantum\n`);
