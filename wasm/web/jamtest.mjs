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
 * Then a second room on a piece whose picture is a control: one page
 * plays, the other enlarges gen::life's board and paints a line across it
 * with a pointer, and the two tapes have to be one tape -- and not the
 * tape of the run nobody painted on (JAM_M6.md, section 8.3).
 *
 * And then the other half of that gate: one page opens an instrument on
 * the .dsp canvas, clicks a node and types a number into it. The document
 * changes, the other page has the same file, and the text is what native
 * NodeEdit writes for the same edit.
 *
 * Live rather than offline, because two peers have to agree on a clock
 * and an offline context has none. A headless browser has no sound card,
 * but it renders an AudioContext in real time all the same, and real time
 * is what the clocks are about.
 *
 * Exit status is the number of failures.
 */

import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
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

/* The desktop's own build, for scripts/dspedit: the reference an edit made
   in a room is held against (JAM_M6.md, section 8.3). */
const nativeBuild = path.resolve(process.argv[4] ?? path.join(top, 'build'));

const PIECE = 'airports.gen';
const SECONDS = 30;

/* The second half: a piece whose picture is a control, painted on from one
   page while the other listens (JAM_M6.md, section 8.3). Shorter, because
   what is under test is agreement and not endurance. */
const PAINT_PIECE = 'colony.gen';
const PAINT_SECONDS = 14;

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

/* Why a page is not ready, in its own words. */
async function why (page)
{
    const s = await page.evaluate(() => window.jam.state())
        .catch((e) => ({ status: `could not be asked: ${e.message}` }));

    if (s.synth === undefined)
        return s.status;

    return `status "${s.status.trim()}", audio context ${s.context}, ` +
           `synth ${s.synth ? 'up' : 'not up'}, ` +
           `piece ${s.piece ? 'loaded' : 'not loaded'}, ` +
           `${s.relaySamples} relay and ${s.audioSamples} audio clock ` +
           `samples of the ${s.wanted} each that Play waits for`;
}

if (!fs.existsSync(path.join(nodeBuild, 'thinksynth.mjs')))
{
    process.stdout.write(`jamtest: no Node module in ${nodeBuild}; build ` +
                         'it first -- see the top of wasm/CMakeLists.txt.\n');
    process.exit(1);
}

/*
 * The second room: colony.gen, whose gen::life stage has a picture that is
 * a control. One page plays; the other enlarges that picture and paints a
 * line across it with a real pointer.
 *
 * Two things have to be true afterwards, and the second is the one that
 * matters. The two tapes have to be one tape -- a gesture is a command,
 * stamped and applied at its time on every peer, so a board painted on one
 * screen is the same board on both. And that tape must NOT be the one
 * genwav composes from the same piece and the same transport commands,
 * which is the run nobody painted on: a click that changed nothing would
 * look exactly like agreement, which is what gen/hands.gen taught this
 * harness the first time round.
 */
async function paintTogether (pages)
{
    const [A, B] = pages;

    for (const { label, page } of pages)
    {
        await page.goto(`${url}?room=jampaint&name=${label}` +
                        `&piece=${PAINT_PIECE}`);
        await page.waitForFunction(
            () => !document.getElementById('roompanel').hidden,
            null, { timeout: 15000 });
        await page.click('#start');
        await page.waitForFunction(() => window.jam.ready(), null,
                                   { timeout: 20000 });
    }

    for (const { page } of pages)
        await page.waitForFunction(
            () => window.jam.peers().every((p) => p.path !== 'connecting'),
            null, { timeout: 15000 }).catch(() => {});

    /* The painter's page: the mirror said what the piece has, and what
       can be painted on is a button. */
    await B.page.waitForSelector('#composerstages button', { timeout: 30000 });
    await B.page.click('#composerstages button');
    await B.page.waitForFunction(
        () => /^Painting /.test(
            document.getElementById('composerstatus').textContent),
        null, { timeout: 15000 });

    ok(`${B.label} enlarged ` +
       `${await B.page.textContent('#composerstages button')}`);

    await A.page.evaluate(() => window.jam.play());

    const t0 = Date.now();
    const at = (ms) => new Promise((r) =>
        setTimeout(r, Math.max(0, t0 + ms - Date.now())));

    await at(3000);

    /* A line across the enlarged board, with the pointer. The scroller's
       box and not the canvas's: the element is as big as the whole
       drawing and the scroller clips it, and the enlarged picture fills
       what can be seen. */
    await B.page.locator('#composerscroll').scrollIntoViewIfNeeded();

    const box = await B.page.$eval('#composerscroll', (d) =>
    {
        const r = d.getBoundingClientRect();

        return { x: r.x, y: r.y, w: d.clientWidth, h: d.clientHeight };
    });

    await B.page.mouse.move(box.x + box.w * 0.3, box.y + box.h * 0.5);
    await B.page.mouse.down();

    for (let i = 1; i <= 8; i++)
    {
        await B.page.mouse.move(box.x + box.w * (0.3 + 0.045 * i),
                                box.y + box.h * 0.5);
        await new Promise((r) => setTimeout(r, 50));
    }

    await B.page.mouse.up();

    await at(PAINT_SECONDS * 1000);
    await A.page.evaluate(() => window.jam.stop());
    await at(PAINT_SECONDS * 1000 + 3000);

    const results = [];

    for (const { label, page } of pages)
        results.push({ label, ...(await page.evaluate(() => ({
            tape: window.jam.tape(),
            sent: window.jam.sent(),
        }))) });

    const sent = results.flatMap((r) => r.sent)
        .filter((c) => c.at >= 0)
        .sort((a, b) => a.at - b.at);
    const painted = sent.filter((c) => c.type === 'input');
    const stopAt = sent.find((c) => c.op === 'stop')?.at;

    if (painted.length === 0)
    {
        fail('nothing was painted, so nothing about painting was tested');
        return;
    }

    ok(`${painted.length} gestures went out as commands, ` +
       `stamped ${painted[0].at.toFixed(3)} to ` +
       `${painted.at(-1).at.toFixed(3)}`);

    if (stopAt === undefined)
    {
        fail('no stop was sent in the painted room');
        return;
    }

    const tapes = results.map((r) => tapeBefore(r.tape, stopAt));

    if (tapes[0] === tapes[1])
        ok('a board painted on one screen is the same board on both: ' +
           `${tapes[0].split('\n').length - 1} events`);
    else
        fail(`the painted tapes differ: ` +
             `${firstDifference(tapes[0], tapes[1])}`);

    const untouched = reference(PAINT_PIECE, nodeBuild,
                                { commands: sent.filter(
                                      (c) => c.type !== 'input'),
                                  knobs: {}, stopAt });

    if (tapes[0] !== untouched)
        ok('and it is not the tape of the run nobody painted on');
    else
        fail('painting the board changed nothing about what it played');
}

/*
 * The .dsp canvas, in a room (JAM_M6.md, sections 7.3 and 8.3).
 *
 * One page opens an instrument on the canvas, clicks a node and types a
 * number into it. Three things have to be true. The document has to change
 * -- an edit on the canvas is a splice into the shared file, not a local
 * copy. The other page has to have the same file, because that is what a
 * shared document means. And the text has to be what native NodeEdit
 * writes for the same edit, byte for byte, because the .dsp somebody
 * changes in a browser is the .dsp somebody else opens in the editor.
 */
async function editTogether (pages)
{
    const [A, B] = pages;

    await A.page.evaluate(() =>
    {
        document.getElementById('nodeview').open = true;
    });

    await A.page.waitForFunction(
        () => window.jam.node()?.boxes > 0, null, { timeout: 30000 })
        .catch(() => {});

    const file = await A.page.$eval('#nodefile', (s) => s.value);
    const graph = await A.page.evaluate(() => window.jam.node());

    if (!file || !graph || graph.boxes === 0)
    {
        fail(`the instrument canvas shows ${graph?.boxes ?? 0} boxes of ` +
             `${file || 'no file'}`);
        return;
    }

    ok(`${A.label} opened ${file} on the canvas: ${graph.boxes} boxes`);

    /* A node with something on it a person could type into. */
    const where = await A.page.evaluate(() =>
    {
        const n = window.jam.node();

        for (let i = 0; i < n.boxes; i++)
        {
            const b = n.box(i);

            if (b.kind === 0 && b.settable)
                return b;
        }

        return null;
    });

    if (where === null)
    {
        fail(`nothing in ${file} has a value to set`);
        return;
    }

    await A.page.locator('#nodescroll').scrollIntoViewIfNeeded();

    const box = await A.page.$eval('#nodecanvas', (c) =>
    {
        const r = c.getBoundingClientRect();

        return { x: r.x, y: r.y };
    });

    await A.page.mouse.click(box.x + where.x + 8, box.y + where.y + 8);
    await A.page.waitForFunction(
        () => window.jam.node().selected >= 0, null, { timeout: 15000 });

    /* The text as it stands after the click and before the value: a click
       on a box is a drag of zero length, and the canvas writes the
       positions out for one exactly as the desktop does, so the file has
       already gained its layout block by now. What is under test below is
       the value, so this is what the desktop is given to edit. */
    const before = await A.page.evaluate(
        (name) => window.jam.file(name), file);

    const input = await A.page.$('#nodeparams input');

    if (input === null)
    {
        fail(`${where.name} has nothing to type into after all`);
        return;
    }

    const arg = await input.evaluate((i) => i.dataset.arg);
    const value = '0.321';

    await input.fill(value);
    await input.press('Enter');

    /* The other page, through the relay. */
    await B.page.waitForFunction(
        ([name, was]) => window.jam.file(name) !== was, [file, before],
        { timeout: 20000 }).catch(() => {});

    const mine = await A.page.evaluate((name) => window.jam.file(name), file);
    const theirs = await B.page.evaluate((name) => window.jam.file(name),
                                         file);

    if (mine === before)
    {
        fail(`typing ${value} into ${where.name}.${arg} changed nothing`);
        return;
    }

    if (mine !== theirs)
    {
        fail(`${B.label}'s copy of ${file} is not ${A.label}'s`);
        return;
    }

    ok(`a value set on the canvas is in both peers' copy of ${file}`);

    /* And it is the edit the desktop makes. */
    const scratch = path.join(os.tmpdir(), 'jamtest-edit.dsp');

    fs.writeFileSync(scratch, before);

    const want = execFileSync(
        path.join(nativeBuild, 'scripts', 'dspedit'),
        [scratch, 'set-value', where.name, arg, value],
        { encoding: 'utf8' });

    if (mine === want)
        ok(`and it is what NodeEdit writes for ${where.name}.${arg} = ` +
           `${value}`);
    else
        fail(`the room's edit of ${where.name}.${arg} is not the one the ` +
             'desktop makes');

    /* And a probe: a right-click on an output port offers the displays
       this build has, and picking one arms a tap in the worklet, opens
       that display in the page's own instance of the module, and hangs a
       panel on the node (JAM_M6.md, section 7.4). The samples themselves
       are gated headlessly in nodecheck; what is under test here is that
       a person can ask for one, and take it away again. */
    const port = await A.page.evaluate(() =>
    {
        const n = window.jam.node();

        for (let i = 0; i < n.boxes; i++)
        {
            const b = n.box(i);
            const out = b.ports.find((p) => !p.isInput);

            if (b.kind === 0 && out)
                return { ...out, name: b.name, port: out.name };
        }

        return null;
    });

    if (port === null)
    {
        fail(`nothing in ${file} has an output to probe`);
        return;
    }

    const boxesWere = graph.boxes;

    await A.page.mouse.click(box.x + port.x, box.y + port.y,
                             { button: 'right' });
    await A.page.waitForFunction(
        () => document.querySelectorAll('#nodemenu button').length > 0,
        null, { timeout: 15000 }).catch(() => {});

    const offered = await A.page.$$eval('#nodemenu button',
                                        (bs) => bs.map((b) => b.textContent));
    const scope = offered.findIndex((t) => t.includes('scope'));

    if (scope < 0)
    {
        fail(`the port menu offered ${offered.join(', ') || 'nothing'}`);
        return;
    }

    ok(`a right-click on ${port.port} offers ${offered.length} displays`);

    await (await A.page.$$('#nodemenu button'))[scope].click();
    await A.page.waitForFunction(
        () => window.jam.node().probes > 0, null, { timeout: 15000 })
        .catch(() => {});

    const after = await A.page.evaluate(() => window.jam.node());

    if (after.probes === 1 && after.boxes === boxesWere + 1)
        ok(`picking one arms it and hangs a panel on ${port.name}`);
    else
    {
        fail(`arming a probe left ${after.probes} probes and ` +
             `${after.boxes} boxes, from ${boxesWere}`);
        return;
    }

    /* And taking it away: the same port again, which now offers to stop
       rather than to start. The port has moved -- a panel makes its host
       taller and pushes everything down -- so where it is now is asked
       for again rather than assumed. */
    const moved = await A.page.evaluate((want) =>
    {
        const n = window.jam.node();

        for (let i = 0; i < n.boxes; i++)
        {
            const b = n.box(i);

            if (b.name !== want.name)
                continue;

            const p = b.ports.find((q) => q.name === want.port);

            if (p !== undefined)
                return p;
        }

        return null;
    }, { name: port.name, port: port.port });

    if (moved === null)
    {
        fail(`${port.name}.${port.port} is not in the graph any more`);
        return;
    }

    const now = await A.page.$eval('#nodecanvas', (c) =>
    {
        const r = c.getBoundingClientRect();

        return { x: r.x, y: r.y };
    });

    await A.page.mouse.click(now.x + moved.x, now.y + moved.y,
                             { button: 'right' });
    await A.page.waitForFunction(
        () => [...document.querySelectorAll('#nodemenu button')]
            .some((b) => b.textContent.startsWith('Stop watching')),
        null, { timeout: 15000 }).catch(() => {});

    const stop = (await A.page.$$('#nodemenu button'))[0];

    if (stop === undefined)
    {
        fail(`nothing offered to stop the probe at ${moved.x},${moved.y}`);
        return;
    }

    await stop.click();
    await A.page.waitForFunction(
        () => window.jam.node().probes === 0, null, { timeout: 15000 })
        .catch(() => {});

    const back = await A.page.evaluate(() => window.jam.node());

    if (back.probes === 0 && back.boxes === boxesWere)
        ok('and stopping it takes the panel away again');
    else
        fail(`stopping left ${back.probes} probes and ${back.boxes} boxes`);
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

        try
        {
            await page.waitForFunction(() => window.jam.ready(), null,
                                       { timeout: 20000 });
        }
        catch
        {
            /* A bare timeout says nothing about which of the three
               conditions is still false, and the answer is usually the
               audio: a machine with no sound card leaves Firefox's
               resume() unresolved, Start never returns, and the page is
               still saying "Starting..." (see the note in the CI wasm
               job about the null sink). */
            fail(`${label} never became ready -- ${await why(page)}`);
            throw new Error(`${label} did not start`);
        }

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

    /* Each page held its worklet's tape against its mirror's, event for
       event, all the way through: two instances of one module on one
       stream of commands have to compose one piece, and a room gets that
       check for nothing (JAM_M6.md, section 4). */
    for (const r of results)
    {
        const line = /tape v mirror\s+(.*)/.exec(r.numbers)?.[1] ?? '';
        const compared = Number(/of (\d+) event/.exec(line)?.[1] ?? 0);

        if (/^none/.test(line) && compared > 0)
            ok(`${r.label}'s mirror composed what its worklet composed, ` +
               `${compared} events`);
        else
            fail(`${r.label}: tape against mirror is "${line}"`);
    }

    /* ---- and now somebody paints on a Life board ---- */

    await paintTogether(pages);

    /* ---- and edits an instrument on the canvas ---- */

    await editTogether(pages);

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
