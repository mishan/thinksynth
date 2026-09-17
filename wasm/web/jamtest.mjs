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
 * jamtest.mjs -- two headless browsers in one room, one tape.
 *
 *   cd wasm/web && npm ci && npx playwright install chromium firefox
 *   node jamtest.mjs [BUILD_DIR] [NODE_BUILD_DIR]
 *
 * M3's second gate (JAM_M3.md, section 8.2), and the wasm-against-wasm
 * gate from M2 with the network in between. A relay and a site are
 * started; a Chromium page and a Firefox page join the same room, each
 * with a live AudioContext; one presses Play; both run a seeded piece for
 * SECONDS of wall clock while this script moves a knob from each side and
 * changes the tempo from one; then both hand over the tape they
 * delivered. Passes when the tapes are identical to each other and to
 * genwav.mjs's for the same piece and the same command stream, and the
 * late count on both is zero.
 *
 * Live rather than offline, because two peers have to agree on a clock
 * and an offline context has none. A headless browser has no sound card,
 * but it renders an AudioContext in real time all the same, and real time
 * is what the clocks are about.
 *
 * Exit status is the number of failures.
 */

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { chromium, firefox } from 'playwright';

import { tapeBefore } from '../tape.mjs';
import { firstDifference, reference } from './piececheck.mjs';
import { relay } from './relay.mjs';
import { serve } from './serve.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const top = path.join(here, '..', '..');
const build = path.resolve(process.argv[2] ?? path.join(top, 'build-web'));
const nodeBuild = path.resolve(process.argv[3] ??
                               process.env.THINK_WASM_BUILD ??
                               path.join(top, 'build-wasm'));

const PIECE = 'airports.gen';
const SECONDS = 30;

let failures = 0;

function fail (what)
{
    failures++;
    process.stdout.write(`FAIL  ${what}\n`);
}

function ok (what)
{
    process.stdout.write(`ok    ${what}\n`);
}

if (!fs.existsSync(path.join(nodeBuild, 'thinksynth.mjs')))
{
    process.stdout.write(`jamtest: no Node module in ${nodeBuild}; build ` +
                         'it first -- see the top of wasm/CMakeLists.txt.\n');
    process.exit(1);
}

if (!fs.existsSync(path.join(build, 'jam.js')))
{
    process.stdout.write(`jamtest: no room page in ${build}; build the ` +
                         'site first -- see wasm/web/CMakeLists.txt.\n');
    process.exit(1);
}

const relayServer = await relay({ port: 0, host: '127.0.0.1', tree: top });
const relayUrl = `ws://127.0.0.1:${relayServer.address().port}`;
const site = await serve(build, 0, '127.0.0.1', relayUrl);
const url = `http://127.0.0.1:${site.address().port}/jam.html`;

const browsers = [];
const pages = [];
const errors = [];

try
{
    for (const [label, type, opts] of [
        ['chromium', chromium,
         { args: ['--autoplay-policy=no-user-gesture-required'] }],
        ['firefox', firefox, {}]])
    {
        const browser = await type.launch(opts);
        const page = await browser.newPage();

        page.on('pageerror', (e) => errors.push(`${label}: ${e.message}`));
        page.on('console', (m) =>
        {
            if (m.type() === 'error')
                errors.push(`${label} console: ${m.text()}`);
        });

        await page.goto(`${url}?room=jamtest&name=${label}&piece=${PIECE}`);
        browsers.push(browser);
        pages.push({ label, page });
    }

    /* Joined, and started: the page joins itself from the URL; Start is
       a click, since an AudioContext wants a gesture. */
    for (const { label, page } of pages)
    {
        await page.waitForFunction(() => !document.getElementById('roompanel').hidden,
                                   null, { timeout: 15000 });
        await page.click('#start');
        await page.waitForFunction(() => window.jam.ready(), null,
                                   { timeout: 20000 });
        ok(`${label} joined the room and started`);
    }

    /* Seen each other, by whatever path -- given the ten seconds the
       mesh gives a channel to open before it falls back. */
    for (const { label, page } of pages)
    {
        await page.waitForFunction(
            () => window.jam.peers().every((p) => p.path !== 'connecting'),
            null, { timeout: 15000 }).catch(() => {});

        const peers = await page.evaluate(() => window.jam.peers());
        const other = peers.find((p) => p.name !== label);

        if (other === undefined)
            fail(`${label} does not see the other peer`);
        else
            ok(`${label} sees ${other.name} (${other.path})`);
    }

    /* Play from the first; knobs from both; a tempo from the second. */
    const [A, B] = pages;

    /* A knob dragged before Play. While the transport is stopped there is
       no time to stamp against, so it goes out as -1 -- "now, on every
       peer" -- and is applied on arrival. Stamped for a transport time
       instead it would sit in every worklet's queue waiting for a clock
       that is not running, and the load a Play does would throw it away
       without counting it. */
    await A.page.evaluate(() => window.jam.knob(0, 0.42));
    await new Promise((r) => setTimeout(r, 300));

    const stopped = await A.page.evaluate(() => window.jam.sent().at(-1));

    if (stopped?.at === -1)
        ok('a knob moved before Play is stamped for now, not for a time');
    else
        fail(`a knob moved before Play was stamped ${stopped?.at}`);

    /* And an edit in flight at the Play: A composes from a revision B has
       not seen yet, so B's start waits for the update before it loads
       (JAM_M3.md, section 4.3), and whatever arrives while it waits has
       to survive the wait rather than be cleared by the load or the arm.
       A comment, so the piece composes exactly as it did. */
    await A.page.click('.cm-content');
    await A.page.keyboard.press('Control+Home');
    await A.page.keyboard.type('# an edit in flight at the Play\n');

    await A.page.evaluate(() => window.jam.play());

    const t0 = Date.now();
    const at = (ms) => new Promise((r) =>
        setTimeout(r, Math.max(0, t0 + ms - Date.now())));

    await at(4000);
    await A.page.evaluate(() => window.jam.knob(0, 0.31));

    /* Where each transport is against the one clock they share. */
    {
        const probes = [];

        for (const { label, page } of pages)
            probes.push({ label, ...(await page.evaluate(
                () => window.jam.probe())) });

        for (const p of probes)
            process.stdout.write(
                `      ${p.label}: transport ${p.transportNow.toFixed(3)} at ` +
                `relay ${(p.relayNow / 1000).toFixed(3)} -> ` +
                `${(p.transportNow - p.relayNow / 1000).toFixed(3)}; ` +
                `currentTime ${p.currentTime?.toFixed(3)}, output ` +
                `${p.output?.contextTime?.toFixed(3)} @ ` +
                `${p.output?.performanceTime?.toFixed(1)} (now ` +
                `${p.performanceNow.toFixed(1)}), fit ${p.fit?.toFixed(3)}, ` +
                `origin ${p.origin}, running ${p.running}, reported ` +
                `${p.reported?.toFixed(3)}\n`);
    }
    await at(8000);
    await B.page.evaluate(() => window.jam.knob(0, 0.77));
    await at(12000);
    await B.page.evaluate(() => window.jam.tempo(100));
    await at(16000);
    await A.page.evaluate(() => window.jam.knob(0, 0.05));
    await at(SECONDS * 1000);
    await A.page.evaluate(() => window.jam.stop());
    await at(SECONDS * 1000 + 3000);

    /* What each delivered, and what each saw. */
    const results = [];

    for (const { label, page } of pages)
        results.push({ label, ...(await page.evaluate(() => ({
            tape: window.jam.tape(),
            late: window.jam.late(),
            sent: window.jam.sent(),
            peers: window.jam.peers(),
            margins: window.jam.margins(),
            log: document.getElementById('log').textContent,
            numbers: document.getElementById('numbers').textContent,
        }))) });

    for (const r of results)
    {
        const remote = r.margins.filter((m) => m.from !== r.sent[0]?.from);
        const least = remote.reduce((a, m) => Math.min(a, m.margin), Infinity);

        process.stdout.write(
            `      ${r.label}: the other peer's commands arrived ` +
            `${remote.map((m) => (m.margin * 1000).toFixed(0)).join(', ')} ms ` +
            `ahead of their time (least ${(least * 1000).toFixed(0)} ms)\n`);
    }

    /* The run's command stream, for genwav. Only what is stamped with a
       transport time: a command made while the transport was stopped
       belongs to no run, and the load a Play does puts the piece back to
       what the file says whatever was moved before it. */
    const sent = results.flatMap((r) => r.sent)
        .filter((c) => (c.type === 'knob' || c.type === 'transport') &&
                       c.at >= 0)
        .sort((a, b) => a.at - b.at);
    const stopAt = sent.find((c) => c.op === 'stop')?.at;

    if (stopAt === undefined)
        fail('no stop was sent');
    else
    {
        const tapes = results.map((r) => tapeBefore(r.tape, stopAt));

        if (tapes[0] === tapes[1])
            ok(`the two tapes are one tape: ` +
               `${tapes[0].split('\n').length - 1} events`);
        else
            fail(`the tapes differ: ${firstDifference(tapes[0], tapes[1])}`);

        /* The knob names, for genwav, by the index a command names one
           by -- which is the module's numbering over every knob the piece
           declared, hidden ones included, and not the position of the
           slider on the page. Each slider carries its own index. */
        const knobs = await A.page.evaluate(() => Object.fromEntries(
            [...document.querySelectorAll('#knobs input')].map(
                (i) => [i.dataset.knob, i.id.replace(/^knob-/, '')])));
        const want = reference(PIECE, nodeBuild,
                               { commands: sent, knobs, stopAt });

        if (tapes[0] === want)
            ok('and it is the tape genwav delivers under the same commands');
        else
            fail(`chromium's tape differs from genwav's: ` +
                 `${firstDifference(want, tapes[0])}`);
    }

    for (const r of results)
    {
        if (r.late.worklet === 0 && r.late.seen === 0)
            ok(`${r.label} applied nothing late`);
        else
            fail(`${r.label} applied ${r.late.worklet} late by the ` +
                 `worklet's count, ${r.late.seen} by the page's` +
                 (r.late.page.length > 0
                      ? ': ' + r.late.page.map((c) =>
                            `${c.from}#${c.seq} ${c.type}` +
                            `${c.op ? ' ' + c.op : ''} at ${c.at}`).join(', ')
                      : '') +
                 (r.log ? `\n      ${r.log.trim().split('\n').join('\n      ')}`
                        : ''));
    }

    for (const e of errors)
        fail(`page error: ${e}`);
}
catch (e)
{
    fail(`threw: ${e.message.split('\n')[0]}`);
}

for (const b of browsers)
    await b.close();

site.closeAllConnections();
site.close();
relayServer.shutdown();

process.stdout.write(`\n${failures === 0
                          ? 'two browsers in one room compose one tape\n'
                          : `${failures} failed\n`}`);
process.exitCode = failures;
