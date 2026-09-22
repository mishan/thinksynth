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
 * compare.mjs -- the native genwav and genwav.mjs, on the same pieces.
 *
 *   node wasm/compare.mjs                   every seeded piece in gen/
 *   node wasm/compare.mjs -s 30 gen/ebb.gen
 *   node wasm/compare.mjs -b build-asan -k /tmp/renders
 *
 * Each piece is rendered by both, and the WAVs, the tapes, the summary
 * lines and the exit statuses are compared byte for byte. Anything short of
 * identical is described rather than just reported: for audio, where it
 * starts, how much of it, and how far apart; for a tape, whether it is the
 * same events in another order or different events.
 *
 * A piece with no `seed' line draws its master seed at random and does not
 * repeat even against itself, so the default set is the pieces that pin
 * one. Name an unseeded piece explicitly and it is compared all the same.
 *
 * -l lets the audio differ by up to N steps per sample and still pass, which
 * is what CI asks for with -l 1. The native build's libm is glibc's and this
 * one's is musl's, the two disagree in the last bit now and then, and now
 * and then that sends a sample to the neighbouring 16-bit value. Tapes,
 * summaries and exit statuses are held to identical whatever -l says: the
 * tape is the composition, and nothing excuses it differing.
 *
 * Exit status is 0 if every piece matched, 1 if any did not.
 */

import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';

import { seeded as pinsSeed } from './tape.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const top = path.resolve(here, '..');

function usage ()
{
    process.stdout.write(
        'usage: compare.mjs [-b NATIVE_BUILD] [-s SECONDS] [-k DIR] [file.gen ...]\n' +
        '\n' +
        '  -b, --build DIR    the native build tree (default build/)\n' +
        '  -s, --seconds N    transport length for both (default 60)\n' +
        '  -l, --lsb N        let the WAVs differ by up to N per sample (default 0)\n' +
        '  -k, --keep DIR     leave the renders here rather than in a temp dir\n' +
        '  -j, --jobs N       pieces to render at once (default half the cores)\n');
}

/* stdout is ignored rather than captured: genwav writes the render to the
   file -o names and nothing here reads its stdout, and an ignored stream
   cannot fill its pipe and stall the child while this waits for exit. */
function run (cmd, args, env)
{
    return new Promise((resolve, reject) =>
    {
        const child = spawn(cmd, args,
                            { cwd: top, env,
                              stdio: ['ignore', 'ignore', 'pipe'] });
        let stderr = '';

        child.stderr.setEncoding('latin1');
        child.stderr.on('data', (d) => { stderr += d; });
        child.on('error', reject);
        child.on('close', (status) => resolve({ status, stderr }));
    });
}

/* Runs `task' over every item, `limit' of them in flight. Each task is two
   renders of one piece, so the process count is twice the limit -- which is
   why the default below halves the core count rather than using it. */
async function pool (items, limit, task)
{
    const results = new Array(items.length);
    let next = 0;

    async function worker ()
    {
        for (let i = next++; i < items.length; i = next++)
            results[i] = await task(items[i], i);
    }

    await Promise.all(Array.from({ length: Math.min(limit, items.length) },
                                 worker));

    return results;
}

function pcm (file)
{
    const b = fs.readFileSync(file);

    return new Int16Array(b.buffer.slice(b.byteOffset + 44,
                                         b.byteOffset + b.length));
}

/* How two WAVs differ, and the largest difference in any one sample -- the
   number -l is held against. */
function describeWav (a, b, rate, channels)
{
    if (a.length !== b.length)
        return { max: Infinity,
                 text: `lengths differ (${a.length} and ${b.length} samples)` };

    let first = -1, count = 0, max = 0, signal = 0, noise = 0;

    for (let i = 0; i < a.length; i++)
    {
        const d = a[i] - b[i];

        signal += a[i] * a[i];

        if (d === 0)
            continue;

        if (first < 0)
            first = i;

        count++;
        noise += d * d;
        max = Math.max(max, Math.abs(d));
    }

    return { max,
             text: `from ${(first / channels / rate).toFixed(3)} s, ` +
                   `${count} samples (${(100 * count / a.length).toFixed(2)}%), ` +
                   `max ${max} LSB, ` +
                   `SNR ${(10 * Math.log10(signal / noise)).toFixed(1)} dB` };
}

function describeTape (a, b)
{
    const la = a.split('\n'), lb = b.split('\n');

    if ([...la].sort().join('\n') === [...lb].sort().join('\n'))
        return 'the same events in another order';

    let k = 0;

    while (k < la.length && la[k] === lb[k])
        k++;

    return `different events from line ${k + 1}: ` +
           `"${la[k] ?? ''}" / "${lb[k] ?? ''}"`;
}

function seeded (file)
{
    return pinsSeed(fs.readFileSync(file, 'utf8'));
}

async function main (args)
{
    let build = path.join(top, 'build');
    let seconds = '60';
    let lsb = 0;
    let keep = '';
    let jobs = Math.max(1, Math.ceil(os.availableParallelism() / 2));
    const pieces = [];

    for (let i = 0; i < args.length; i++)
    {
        const a = args[i];

        if ((a === '-b' || a === '--build') && i + 1 < args.length)
            build = path.resolve(args[++i]);
        else if ((a === '-s' || a === '--seconds') && i + 1 < args.length)
            seconds = args[++i];
        else if ((a === '-l' || a === '--lsb') && i + 1 < args.length &&
                 /^\d+$/.test(args[i + 1]))
            lsb = parseInt(args[++i], 10);
        else if ((a === '-k' || a === '--keep') && i + 1 < args.length)
            keep = path.resolve(args[++i]);
        else if ((a === '-j' || a === '--jobs') && i + 1 < args.length &&
                 /^[1-9]\d*$/.test(args[i + 1]))
            jobs = parseInt(args[++i], 10);
        else if (a === '-h' || a === '--help')
        {
            usage();
            return 0;
        }
        else if (a.startsWith('-'))
        {
            usage();
            return 2;
        }
        else
            pieces.push(path.resolve(a));
    }

    if (pieces.length === 0)
    {
        const dir = path.join(top, 'gen');

        for (const f of fs.readdirSync(dir).sort())
            if (f.endsWith('.gen') && seeded(path.join(dir, f)))
                pieces.push(path.join(dir, f));
    }

    const native = path.join(build, 'scripts', 'genwav');

    if (!fs.existsSync(native))
    {
        process.stderr.write(`compare.mjs: no ${native} -- build genwav ` +
                             'and the plugins first, or pass -b\n');
        return 2;
    }

    const out = keep !== '' ? keep :
        fs.mkdtempSync(path.join(os.tmpdir(), 'genwav-compare-'));

    fs.mkdirSync(out, { recursive: true });

    const env = { ...process.env };

    env.THINK_DSP_PATH ??= path.join(top, 'dsp');

    /* Each piece is a task, and `jobs' of them are in flight at once. The
       renders are two processes that share nothing -- separate output
       files, separate address spaces -- so what they contend for is cores,
       and a piece's verdict does not depend on which other piece is
       running beside it.

       A finished piece writes its whole block in one call rather than a
       line at a time: the header names the piece and the lines under it
       are indented continuations of that name, so they have to stay
       together to be read at all. Which block comes first varies with
       which render finished first. */
    const outcomes = await pool(pieces, jobs, async (gen, index) =>
    {
        const name = path.basename(gen, '.gen');

        /* Numbered, so two pieces of the same name from two directories do
           not write over each other's renders. */
        const stem = `${String(index + 1).padStart(2, '0')}-${name}`;
        const f = (which, ext) => path.join(out, `${stem}.${which}.${ext}`);
        const rel = path.relative(top, gen);

        /* Nothing left from an earlier run into the same -k directory: a
           render that fails before it writes must find nothing to be
           compared against, not the last run's files. */
        for (const which of ['native', 'wasm'])
            for (const ext of ['wav', 'tape'])
                fs.rmSync(f(which, ext), { force: true });

        const missing = (ext) =>
            ['native', 'wasm'].filter((which) => !fs.existsSync(f(which, ext)));

        /* --levels and --sections on both sides: the two tables are
           formatted by hand in C and again in JS, down to the column
           widths and the half-to-even rounding, and the stderr
           comparison below is the only thing that can notice them
           drifting apart. The numbers behind them are sums over the
           same PCM, which this already demands be byte-identical. */
        const [n, w] = await Promise.all([
            run(native, ['-p', path.join(build, 'plugins') + '/',
                         '-s', seconds, '--levels', '--sections',
                         '-o', f('native', 'wav'),
                         '-t', f('native', 'tape'), rel], env),
            run(process.execPath,
                [path.join(here, 'genwav.mjs'), '-s', seconds,
                 '--levels', '--sections',
                 '-o', f('wasm', 'wav'), '-t', f('wasm', 'tape'), rel],
                env)]);

        const problems = [], within = [];

        /* stderr is the summary line and then the two tables. One line
           is what a report of a piece that matched has room for; where
           they differ, the first line that does is what to say. */
        const summary = (t) => t.split('\n')[0].trim();

        if (n.status !== w.status)
            problems.push(`exit ${n.status} and ${w.status}`);

        if (n.stderr !== w.stderr)
        {
            const a = n.stderr.split('\n'), b = w.stderr.split('\n');
            let i = 0;

            while (i < a.length && i < b.length && a[i] === b[i])
                i++;

            problems.push(`report line ${i + 1}: ` +
                          `"${(a[i] ?? '').trim()}" / "${(b[i] ?? '').trim()}"`);
        }

        /* A tape that was not written is a tape that differs. */
        if (missing('tape').length > 0)
            problems.push(`no tape from ${missing('tape').join(' or ')}`);
        else
        {
            const a = fs.readFileSync(f('native', 'tape'), 'latin1');
            const b = fs.readFileSync(f('wasm', 'tape'), 'latin1');

            if (a !== b)
                problems.push(`tape: ${describeTape(a, b)}`);
        }

        if (missing('wav').length > 0)
            problems.push(`no WAV from ${missing('wav').join(' or ')}`);
        else
        {
            const hdr = fs.readFileSync(f('native', 'wav')).subarray(0, 44);
            const a = pcm(f('native', 'wav')), b = pcm(f('wasm', 'wav'));

            if (!hdr.equals(fs.readFileSync(f('wasm', 'wav')).subarray(0, 44)))
                problems.push('wav: the headers differ');
            else if (!Buffer.from(a.buffer).equals(Buffer.from(b.buffer)))
            {
                const d = describeWav(a, b, hdr.readUInt32LE(24),
                                      hdr.readUInt16LE(22));

                (d.max <= lsb ? within : problems).push('wav: ' + d.text);
            }
        }

        const label = name.padEnd(10);
        const lines = (list) => list.map((p) => `           ${p}\n`).join('');

        if (problems.length > 0)
        {
            process.stdout.write(`${label} DIFFERS\n` + lines(problems) +
                                 lines(within));
            return 'failed';
        }

        if (within.length > 0)
        {
            process.stdout.write(`${label} within ${lsb} LSB\n` +
                                 lines(within));
            return 'tolerated';
        }

        process.stdout.write(`${label} identical  (${summary(n.stderr)})\n`);

        return 'identical';
    });

    const failed = outcomes.filter((o) => o === 'failed').length;
    const tolerated = outcomes.filter((o) => o === 'tolerated').length;

    process.stdout.write(`\n${pieces.length - failed - tolerated} of ` +
                         `${pieces.length} identical` +
                         (tolerated > 0 ? `, ${tolerated} within ${lsb} LSB` : '') +
                         (failed > 0 ? `, ${failed} differ` : '') +
                         `; renders in ${out}\n`);

    return failed > 0 ? 1 : 0;
}

process.exitCode = await main(process.argv.slice(2));
