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

console.log(`\n${failed} failure${failed === 1 ? '' : 's'}`);
process.exit(failed);
