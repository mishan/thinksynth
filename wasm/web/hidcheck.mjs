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
 * hidcheck.mjs -- Valve Index controllers through the browser's module,
 * from Node, held to the native build's answers.
 *
 *   node wasm/web/hidcheck.mjs NATIVE_HIDACTIONS [BUILD_DIR]
 *
 * scripts/hid's captures played through thinkhid (thinkhid.cpp) as
 * scripts/hidactions plays them natively -- a dongle's as a pair's right
 * hand, till it says its controller has gone, then a wired controller's
 * input and IMU, 4 ms a report -- and every line of what it asks to play,
 * and of what the controller pane shows after each capture, compared with
 * that tool's. The two build one host (hid/thhHost.h) from the same
 * source, so a line that differs is the browser's build reading a report,
 * a profile or a sum otherwise.
 *
 * Exit status is the number of failures.
 */

import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const top = path.join(here, '..', '..');
const native = process.argv[2];
const build = path.resolve(process.argv[3] ?? path.join(top, 'build-web'));

if (!native)
{
    console.error('usage: hidcheck.mjs NATIVE_HIDACTIONS [BUILD_DIR]');
    process.exit(2);
}

const captures = ['index-wireless.txt', 'index-input.txt', 'index-imu.txt']
    .map((f) => path.join(top, 'scripts', 'hid', f));
const want = execFileSync(native, captures, { encoding: 'utf8' })
    .split('\n').filter((l) => l !== '');

const { default: createThinkHid } =
    await import(pathToFileURL(path.join(build, 'thinkhid.js')).href);
const M = await createThinkHid();

const reports = (file) => fs.readFileSync(file, 'utf8').split('\n')
    .filter((l) => l !== '')
    .map((l) => Uint8Array.from(l.slice(l.indexOf('\t') + 1).match(/../g),
                                (h) => parseInt(h, 16)));

const got = [];
let t = 1;
const take = (always) =>
{
    const acts = M.ccall('th_take', 'string', [], []);

    if (always || acts !== '[]')
        got.push(acts);
};
const play = (id, from, file) =>
{
    for (const r of reports(file))
    {
        t += 0.004;
        M.ccall('th_report', 'number',
                ['number', 'number', 'array', 'number', 'number'],
                [id, from, r, r.length, t]);
        M.ccall('th_tick', 'number', ['number'], [t]);
        take(false);
    }
};

const add = (hand, serial) => M.ccall('th_add', 'number',
                                      ['string', 'string', 'number'],
                                      [hand, serial, t]);
const dongle = add('right', 'LHR-R');
const wired = add('', '');

play(dongle, 2, captures[0]);
got.push(M.ccall('th_pane', 'string', [], []));
t += 0.004;
if (M.ccall('th_report', 'number',
            ['number', 'number', 'array', 'number', 'number'],
            [dongle, 2, [0x26, 0x01], 2, t]))
    M.ccall('th_drop', null, ['number', 'number'], [dongle, t]);
take(true);

play(wired, 0, captures[1]);
play(wired, 1, captures[2]);
got.push(M.ccall('th_pane', 'string', [], []));
M.ccall('th_release', null, ['number'], [t += 0.004]);
take(true);

let failed = 0;
const report = (name, why) =>
{
    console.log(why ? `FAIL  ${name}\n  ${why}` : `ok    ${name}`);
    failed += why ? 1 : 0;
};

const first = want.findIndex((w, i) => got[i] !== w);

report('the captures play the same through the browser\'s module',
       want.length === 0 ? 'the native tool said nothing' :
       first < 0 && got.length === want.length ? '' :
       first < 0 ? `${got.length} lines, natively ${want.length}` :
       `line ${first + 1} differs:\n    ${got[first]}\n  natively\n    ` +
       want[first]);
report('notes and values among them',
       want.some((w) => w.includes('"kind":"on"')) &&
       want.some((w) => w.includes('"kind":"value"')) ? '' :
       'no note or no value in what was played');

/* The captures' reports broken at random, the same way every run, through
   a fresh module: no trap, and every answer JSON whose numbers are finite
   and whose notes are MIDI's -- a NaN printed as `nan' is no JSON at all.
   Natively the sanitizers hold the decoder to its buffers (scripts/hidcheck);
   here it is the browser's build that answers. */
{
    const F = await createThinkHid();
    const seeds = captures.map(reports);
    let rng = 1, at = 1, why = '';
    const next = () =>
    {
        rng ^= rng << 13;
        rng >>>= 0;
        rng ^= rng >>> 17;
        rng ^= rng << 5;
        return (rng >>>= 0);
    };
    const add = (serial) => F.ccall('th_add', 'number',
                                    ['string', 'string', 'number'],
                                    ['', serial, at]);
    const ids = [add('A'), add('B')];
    const sane = (text) =>
    {
        for (const a of JSON.parse(text))
        {
            for (const k of ['channel', 'note', 'velocity', 't', 'aux', 'min', 'max'])
                if (k in a && !Number.isFinite(a[k]))
                    return `${k} is ${a[k]}`;
            if ((a.kind === 'on' || a.kind === 'off') &&
                !(a.note >= 0 && a.note <= 127 && a.velocity >= 0 && a.velocity <= 127))
                return `note ${a.note}, velocity ${a.velocity}`;
        }
        return '';
    };

    for (let n = 0; n < 20000 && why === ''; n++)
    {
        /* thhHost::From: a wired input's, its IMU's, a dongle's. */
        const from = next() % 3;
        const pool = seeds[[1, 2, 0][from]];
        const r = Uint8Array.from(pool[next() % pool.length]);
        let len = r.length;

        for (let e = 1 + next() % 4; e > 0 && len > 0; e--)
            switch (next() % 4)
            {
            case 0: r[next() % len] ^= 1 << next() % 8; break;
            case 1: r[next() % len] = next() & 0xff; break;
            case 2: r[Math.min(2, len - 1)] = next() % 32; break;
            case 3: len = next() % (len + 1); break;
            }

        const k = next() % 2;

        at += 0.004;
        try
        {
            const gone = F.ccall('th_report', 'number',
                                 ['number', 'number', 'array', 'number', 'number'],
                                 [ids[k], from, r.subarray(0, len), len, at]);

            F.ccall('th_tick', 'number', ['number'], [at]);
            why = sane(F.ccall('th_take', 'string', [], []));
            JSON.parse(F.ccall('th_pane', 'string', [], []));
            if (gone)
            {
                F.ccall('th_drop', null, ['number', 'number'], [ids[k], at]);
                ids[k] = add(`C${n}`);
            }
        }
        catch (e)
        {
            why = `threw: ${e.message}`;
        }
        if (why !== '')
            why += `, at the ${n + 1}th`;
    }
    report('20,000 broken reports through the browser\'s module', why);
}

console.log(`\n${failed} failure${failed === 1 ? '' : 's'}`);
process.exit(failed);
