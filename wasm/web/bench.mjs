#!/usr/bin/env node
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
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * bench.mjs -- what one quantum costs with a piece running.
 *
 *   node wasm/web/bench.mjs [--json out.json] [--only a.gen,b.gen]
 *                           [--seconds 60] [BUILD_DIR]
 *
 * The one measurement that could send the scheduler back out of the
 * worklet. A quantum is 128 frames, 2.67 ms at 48 kHz, and it is a
 * deadline: whatever happens inside process() has to finish inside that or
 * the page hears it. What happens inside it here is the synth's render, the
 * scheduler's step once per window, every note that step delivered being
 * built into the graph, and -- since the player is still playing -- a
 * chord's worth of note-ons on top.
 *
 * So each piece is run twice: as it is, and with a six-note chord pressed
 * and released over and over, which is the same thing a key costs and the
 * thing that allocates (thinkweb.cpp, applyDue). What is reported is the
 * distribution of the per-quantum time and the worst one, against the
 * budget.
 *
 * Each piece is played the way the page plays it, or a piece that leans on
 * the page measures as silence: the kit's samples handed over, and every
 * channel the piece leaves to the page given its default patch. A piece
 * that pins no seed is given SEED below, so that two runs time the same
 * composition rather than two the host happened to draw.
 *
 * Node rather than a browser on purpose: this is the engine's cost, and
 * the engine is the same wasm either way. What a browser adds on top is the
 * port and the de-interleave, and browsertest.mjs already holds a browser's
 * render against this module's to the sample. What a browser does *not*
 * add, and this is the point of the placement, is a timer, a lookahead or
 * anything a background tab can throttle.
 *
 * The number that decides anything is this run on the slowest machine the
 * thing is meant to play on, not on the one it was written on. --json
 * writes every run's numbers and the machine they came from, so runs on
 * two machines can be set side by side.
 */

import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { parseArgs } from 'node:util';

import { defaults, instruments, pieces, samples } from './piececheck.mjs';
import { aim, loadPiece } from './render.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const { values: opts, positionals } =
    parseArgs({ options: { json: { type: 'string' },
                           only: { type: 'string' },
                           seconds: { type: 'string', default: '60' } },
                allowPositionals: true });
const build = path.resolve(positionals[0] ??
                           path.join(here, '..', '..', 'build-web'));
const SECONDS = Number(opts.seconds);

if (!(Number.isFinite(SECONDS) && SECONDS > 0))
{
    process.stderr.write(`bench: --seconds ${opts.seconds} is not a time\n`);
    process.exit(2);
}

/* Opened now, so a path that cannot be written is known before the
   minutes of timing rather than after. */
const jsonOut = opts.json !== undefined ? fs.openSync(opts.json, 'w') : null;

const { default: createThinkWeb } =
    await import(pathToFileURL(path.join(build, 'thinkweb.js')).href);

const RATE = 48000;
const WINDOW = 256;
const QUANTUM = 128;

/* Any fixed seed would do; what matters is that it is fixed. */
const SEED = 1;

/* A chord, pressed and released twice a second: six voices is a hand. */
const CHORD = [48, 55, 60, 64, 67, 72];
const CHORD_EVERY = 0.5;

function quantile (sorted, q)
{
    return sorted[Math.min(sorted.length - 1,
                           Math.floor(q * sorted.length))];
}

const ms = (x) => `${x.toFixed(3)} ms`;

async function run (gen, dsps, kit, patchFor, chord)
{
    const { M, ok, errors } =
        await loadPiece(createThinkWeb, { rate: RATE, windowlen: WINDOW,
                                          block: QUANTUM, gen,
                                          instruments: dsps, samples: kit,
                                          seed: SEED });

    if (!ok)
        return { errors };

    /* Kept: a channel left unaimed is timed as silence, which is cheaper
       than the page would play it. */
    const log = [];
    const { unaimed } = aim(M, patchFor, log);

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
             moving, unaimed, log };
}

const budget = QUANTUM / RATE * 1000;
const dsps = instruments(build);
const kit = samples(build);
const patchFor = defaults(build);
const only = opts.only?.split(',');

process.stdout.write(
    `a quantum of ${QUANTUM} frames at ${RATE} Hz is ${budget.toFixed(2)} ms, ` +
    `and that is the deadline\n` +
    `the synth runs in windows of ${WINDOW}, so one quantum in ` +
    `${WINDOW / QUANTUM} carries a scheduler step\n\n` +
    `piece            chord        p50       p99     p99.9       max    ` +
    `of quantum   worst at\n`);

const rows = [];
const failed = [];
const all = pieces(build);

/* Kept with the failures, so that a misspelled name fails the run and is
   in --json's document, rather than timing nothing. */
for (const name of only ?? [])
{
    if (!all.some((p) => p.name === name))
    {
        process.stdout.write(`${name}: no such piece\n`);
        failed.push({ name, chord: null, errors: ['no such piece'] });
    }
}

for (const piece of all)
{
    if (only !== undefined && !only.includes(piece.name))
        continue;

    for (const chord of [false, true])
    {
        const r = await run(piece.text, dsps, kit, patchFor, chord);

        if (r.errors !== undefined)
        {
            process.stdout.write(`${piece.name}: did not load -- ` +
                                 `${r.errors.join('; ')}\n`);
            failed.push({ name: piece.name, chord, errors: r.errors });
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

        if (!chord && r.unaimed.length > 0)
            process.stdout.write(
                `  timed as silence: channel ` +
                `${r.unaimed.map((u) => `${u.channel + 1} ` +
                                        `(${u.wanted || 'no default'})`)
                    .join(', ')}` +
                `${r.log.map((l) => `\n  ${l}`).join('')}\n`);
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
if (top !== undefined)
{
    const running = [...rows].sort((a, b) => b.moving - a.moving)[0];

    process.stdout.write(
        `\nworst of all: ${top.name} at ${top.worst.toFixed(3)} ms, ` +
        `${(top.worst / budget * 100).toFixed(1)}% of the quantum, ` +
        `${top.worstAt.toFixed(2)} s in\n` +
        `worst once the transport is moving: ${running.name} at ` +
        `${running.moving.toFixed(3)} ms, ` +
        `${(running.moving / budget * 100).toFixed(1)}% of the quantum\n`);
}

if (jsonOut !== null)
{
    /* This script's checkout, which need not be what the build was made
       from: the build is named by its path and the hash of the module
       that was timed. */
    const git = (...args) =>
    {
        try
        {
            return execFileSync('git', ['-C', here, ...args],
                                { encoding: 'utf8',
                                  stdio: ['ignore', 'pipe', 'ignore'] })
                .trim();
        }
        catch
        {
            /* Not a checkout, or no git: the run is still worth having. */
            return null;
        }
    };
    const status = git('status', '--porcelain', '--untracked-files=no');

    /* By piece and then chord, not by time, so that two documents line
       up run for run. */
    const runs = [
        ...rows.map((r) => ({
            piece: r.name, chord: r.chord, notes: r.notes,
            p50Ms: quantile(r.times, 0.5), p99Ms: quantile(r.times, 0.99),
            p999Ms: quantile(r.times, 0.999), maxMs: r.worst,
            worstAtS: r.worstAt, movingMaxMs: r.moving,
            unaimed: r.unaimed.map((u) => ({ channel: u.channel + 1,
                                             wanted: u.wanted })),
            aimLog: r.log })),
        ...failed.map((f) => ({ piece: f.name, chord: f.chord,
                                error: f.errors.join('; ') })),
    ].sort((a, b) => (a.piece > b.piece) - (a.piece < b.piece) ||
                     a.chord - b.chord);

    fs.writeFileSync(jsonOut, JSON.stringify({
        rate: RATE, windowlen: WINDOW, quantum: QUANTUM, seconds: SECONDS,
        seed: SEED, budgetMs: budget,
        env: { node: process.version, cpu: os.cpus()[0]?.model ?? null,
               benchCommit: git('rev-parse', 'HEAD'),
               benchDirty: status === null ? null : status !== '',
               build,
               wasmSha256: createHash('sha256')
                   .update(fs.readFileSync(path.join(build, 'thinkweb.wasm')))
                   .digest('hex') },
        runs }, null, 2) + '\n');
}

process.exit(failed.length > 0 || rows.length === 0 ? 1 : 0);
