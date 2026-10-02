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
 * The room's second gate: the wasm-against-wasm comparison with the network
 * in between. A relay and a site are started; a Chromium page and a Firefox
 * page join the same room, each with a live AudioContext; one presses Play;
 * both run a seeded piece for SECONDS of wall clock while this script moves
 * a knob from each side and changes the tempo from one; then both hand over
 * the tape they delivered. Passes when the tapes are identical to each
 * other and to genwav.mjs's for the same piece and the same command stream,
 * and the late count on both is zero.
 *
 * Ten seconds in, the first page changes a chain in the document and
 * presses Apply: the edit goes out stamped for the next bar, and every
 * peer puts the new text into the piece there.
 *
 * Partway through, a third page -- another Chromium -- joins the room that
 * is already playing and presses Start. It catches up from the relay's log
 * of the run, moves the knob itself once it has, and its tape, from the
 * top and not only from its arrival, has to be the other two's.
 *
 * Then a second room on a piece whose picture is a control: one page
 * plays, the other enlarges gen::life's board and paints a line across it
 * with a pointer, and the two tapes have to be one tape -- and not the
 * tape of the run nobody painted on.
 *
 * Then a room on hands.gen, played from both pages -- one quantised, one a
 * bar ahead -- whose tape has to be one tape and genwav's.
 *
 * Then a room on cloud.gen, whose recordings both pages have to load.
 *
 * And then the other half of that gate: one page opens an instrument on
 * the .dsp canvas, clicks a node and types a number into it. The document
 * changes, the other page has the same file, and the text is what native
 * NodeEdit writes for the same edit.
 *
 * And last a room on seq.gen, where one page clicks a cell in its Sequencer
 * pane after an edit has moved the track: the same tape on both pages, not
 * the untouched one, and the same cell in both documents.
 *
 * Live rather than offline, because two peers have to agree on a clock
 * and an offline context has none. A headless browser has no sound card,
 * but it renders an AudioContext in real time all the same, and real time
 * is what the clocks are about.
 *
 * Exit status is the number of failures.
 */

/* Under scripts/headless.sh unless THINK_TEST_HEADLESS=0: see headless.mjs. */
import './headless.mjs';

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
   in a room is held against. */
const nativeBuild = path.resolve(process.argv[4] ?? path.join(top, 'build'));

const PIECE = 'airports.gen';
const SECONDS = 30;

/* When the third page arrives, and when it moves the knob, in seconds
   from Play. */
const JOIN_AT = 18;
const JOINER_KNOB_AT = 26;

/* The second half: a piece whose picture is a control, painted on from one
   page while the other listens. Shorter, because what is under test is
   agreement and not endurance. */
const PAINT_PIECE = 'colony.gen';
const PAINT_SECONDS = 14;

/* A piece of grids, clicked on from the Sequencer pane. */
const SEQ_PIECE = 'seq.gen';
const SEQ_SECONDS = 10;

/* The third: a piece nothing plays but people, played from both pages. */
const HANDS_PIECE = 'hands.gen';
const PLAY_SECONDS = 10;

/* The fourth: a piece whose instrument plays recordings. */
const SAMPLE_PIECE = 'cloud.gen';
const SAMPLE_SECONDS = 4;

/* The fifth: a free room, where a seat's instrument is picked while it
   plays. */
const FREE_PIECE = 'free.gen';

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
        await page.goto(`${url}&room=jampaint&name=${label}` +
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
    /* The Life board's, by name, from the Paint menu: colony's euclid
       ring is a control too, and comes first. */
    const board = 'life in colony';

    await B.page.waitForSelector('#composerpaint:not([hidden])',
                                 { timeout: 30000 });
    await B.page.selectOption('#composerpaint', { label: board });
    await B.page.waitForFunction(
        () => /^Painting /.test(
            document.getElementById('composerstatus').textContent),
        null, { timeout: 15000 });

    ok(`${B.label} enlarged ${board}`);

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

    /* And the other page edits a stage's parameter while the piece runs.
     *
     * The same shape of thing one noun along: a param is heard, so a
     * command carries the transport time it applies at and every peer
     * applies it there. If it did not -- if it were applied on arrival, as
     * a chanarg is -- the two peers would change `line's ring a window or
     * two apart and their tapes would part from that point. The tape
     * comparison below is what says they did not.
     *
     * From A, which has enlarged nothing, so its canvas is showing every
     * stage and its params handle is where the canvas says. */
    const pieceFile = await A.page.evaluate(() => window.jam.piece());
    const docWas = await A.page.evaluate(
        (name) => window.jam.file(name), pieceFile);
    const edited = await editParam(A);

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

    const params = sent.filter((c) => c.type === 'param');

    if (!edited || params.length === 0)
        fail('no stage param was set, so nothing about setting one was '
             + 'tested');
    else
    {
        ok(`${A.label} set ${params[0].row} on stage ` +
           `${params[0].chain}.${params[0].stage} to ${params[0].text}, ` +
           `stamped ${params[0].at.toFixed(3)}`);

        /* And the document says so, on both pages, once. Every peer applies
           the edit to its own copy of the piece; the next Start, and a peer
           joining mid-run, read the document -- so an edit that stayed in
           the worklets was gone at the next load. Once, because two peers
           splicing the same change would insert it twice. */
        const docs = await Promise.all(pages.map(({ page }) =>
            page.waitForFunction(([name, was]) =>
                window.jam.file(name) !== was, [pieceFile, docWas],
                { timeout: 15000 }).catch(() => {})
                .then(() => page.evaluate(
                    (name) => window.jam.file(name), pieceFile))));

        const wasLines = docWas.split('\n');
        const nowLines = docs[0].split('\n');
        const changed = nowLines.filter((l, i) => l !== wasLines[i]);

        if (docs[0] !== docs[1])
            fail(`the two pages' documents differ after the edit: ` +
                 `${firstDifference(docs[0], docs[1])}`);
        else if (nowLines.length !== wasLines.length ||
                 changed.length !== 1 ||
                 !changed[0].includes(params[0].row) ||
                 !changed[0].includes(params[0].text))
            fail(`the edit is not the one line of ${pieceFile} that ` +
                 `changed: ${JSON.stringify(changed)}`);
        else
            ok(`and ${pieceFile} carries it on both pages: ` +
               `${changed[0].trim()}`);
    }

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
 * A room on seq.gen, and a click in one page's Sequencer pane: an empty
 * step of the snare's track. The pane is a second door to the same
 * command as the composer canvas's, so the same three things have to hold
 * -- one tape, which is not the tape of the run nobody clicked in, and the
 * cell written into the document once, the same on both pages. And the
 * command has to carry the stage's names, which is what lets it reach its
 * stage across an edit.
 *
 * Before the click, an edit puts a grid chain above the snare's: every
 * index after it moves, and the pane has to follow with a track for the
 * new chain and the snare's picture under the snare's name.
 */
async function sequenceTogether (pages)
{
    const [A, B] = pages;

    for (const { label, page } of pages)
    {
        await page.goto(`${url}&room=jamseq&name=${label}` +
                        `&piece=${SEQ_PIECE}`);
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

    /* In view, which is what has the pane asking for frames -- and a track
       is sized by its first frame, so a click before one is a click on a
       picture of no size. The default canvas is 150 tall and no track is. */
    await B.page.locator('#seqview').scrollIntoViewIfNeeded();

    const sized = await B.page.waitForFunction(() =>
    {
        const grids = document.querySelectorAll('#tracks canvas.trackgrid');

        return grids.length === 6 &&
               [...grids].every((c) => c.height !== 150);
    }, null, { timeout: 15000 }).then(() => true, () => false);

    if (!sized)
    {
        fail(`${B.label}'s Sequencer pane did not show seq.gen's six tracks`);
        return;
    }

    const pieceFile = await A.page.evaluate(() => window.jam.piece());

    await A.page.evaluate(() => window.jam.play());

    const t0 = Date.now();
    const at = (ms) => new Promise((r) =>
        setTimeout(r, Math.max(0, t0 + ms - Date.now())));

    await at(2000);

    const inserted = await A.page.evaluate(() =>
    {
        const name = window.jam.piece();
        const was = window.jam.file(name);
        const next = was.replace('chain snare {',
            'chain rim {\n    stage seq gen::grid {\n' +
            '        notes = "C#2"; steps = 16; rows = 1;\n' +
            '        cells = "......x.......x.";\n' +
            '        period = 0.25 beats; hold = 0.1 beats; listen = 0;\n' +
            '    };\n    sink { instrument = hat; };\n};\n\n' +
            'chain snare {');

        if (next === was)
            return false;

        window.jam.setFile(name, next);
        window.jam.apply();

        return true;
    });

    const followed = inserted && await B.page.waitForFunction(() =>
    {
        const grids = document.querySelectorAll('#tracks canvas.trackgrid');

        return window.jam.edits() === 1 && grids.length === 7 &&
               [...grids].every((c) => c.height !== 150);
    }, null, { timeout: 15000 }).then(() => true, () => false);

    if (!followed)
    {
        const now = await B.page.evaluate(() => [window.jam.edits(),
            document.querySelectorAll('#tracks canvas.trackgrid').length]);

        fail(`${B.label}'s Sequencer did not follow the edit that put a ` +
             `chain above the snare: ${now[0]} edits, ${now[1]} tracks`);
        return;
    }

    ok(`${B.label}'s Sequencer has a track for the chain an edit put ` +
       'above the snare');

    await A.page.waitForFunction(() => window.jam.edits() === 1, null,
                                 { timeout: 15000 }).catch(() => {});

    const docWas = await A.page.evaluate(
        (name) => window.jam.file(name), pieceFile);

    await at(5000);

    /* The snare's third step, which the file leaves empty: the third
       track now, after the kick and the chain the edit put in. */
    const box = await B.page.$eval('#tracks .track:nth-child(3) canvas',
                                   (c) =>
    {
        const r = c.getBoundingClientRect();

        return { x: r.x, y: r.y, w: r.width, h: r.height };
    });

    await B.page.mouse.click(box.x + box.w * 2.5 / 16, box.y + box.h / 2);

    await at(SEQ_SECONDS * 1000);
    await A.page.evaluate(() => window.jam.stop());
    await at(SEQ_SECONDS * 1000 + 3000);

    const results = [];

    for (const { label, page } of pages)
        results.push({ label, ...(await page.evaluate(() => ({
            tape: window.jam.tape(),
            sent: window.jam.sent(),
        }))) });

    const sent = results.flatMap((r) => r.sent)
        .filter((c) => c.at >= 0)
        .sort((a, b) => a.at - b.at);
    const clicked = sent.filter((c) => c.type === 'input');
    const stopAt = sent.find((c) => c.op === 'stop')?.at;

    if (clicked.length === 0 || stopAt === undefined)
    {
        fail(`the Sequencer pane sent ${clicked.length} gestures and the ` +
             `room ${stopAt === undefined ? 'no' : 'a'} stop`);
        return;
    }

    if (clicked.every((c) => c.chainName === 'snare' &&
                             c.stageName === 'seq' && c.rev >= 0))
        ok(`a click on the snare's track went out as ${clicked.length} ` +
           'gestures naming snare.seq');
    else
        fail(`the Sequencer's gestures name ${JSON.stringify(
            clicked.map((c) => [c.chainName, c.stageName, c.rev]))}`);

    const tapes = results.map((r) => tapeBefore(r.tape, stopAt));

    if (tapes[0] === tapes[1])
        ok('a cell clicked in one Sequencer is heard on both pages: ' +
           `${tapes[0].split('\n').length - 1} events`);
    else
        fail(`the sequenced tapes differ: ` +
             `${firstDifference(tapes[0], tapes[1])}`);

    const untouched = reference(SEQ_PIECE, nodeBuild,
                                { commands: sent.filter(
                                      (c) => c.type !== 'input'),
                                  knobs: {}, stopAt });

    if (tapes[0] !== untouched)
        ok('and it is not the tape of the run nobody clicked in');
    else
        fail('clicking the snare\'s track changed nothing about what played');

    const docs = await Promise.all(pages.map(({ page }) =>
        page.waitForFunction(([name, was]) =>
            window.jam.file(name) !== was, [pieceFile, docWas],
            { timeout: 15000 }).catch(() => {})
            .then(() => page.evaluate(
                (name) => window.jam.file(name), pieceFile))));

    const wasLines = docWas.split('\n');
    const nowLines = docs[0].split('\n');
    const changedAt = nowLines.flatMap((l, i) => l !== wasLines[i] ? [i] : []);
    const changed = changedAt.map((i) => nowLines[i]);
    const chainOf = (i) => nowLines.slice(0, i + 1).reverse()
        .find((l) => /^chain /.test(l));

    if (docs[0] !== docs[1])
        fail(`the two pages' documents differ after the click: ` +
             `${firstDifference(docs[0], docs[1])}`);
    else if (nowLines.length !== wasLines.length || changed.length !== 1 ||
             chainOf(changedAt[0]) !== 'chain snare {' ||
             !/^\s*cells\s*=\s*"..x.x.......x...";/.test(changed[0]))
        fail(`the click is not the snare's cells in ${pieceFile}: ` +
             JSON.stringify(changed));
    else
        ok(`and ${pieceFile} carries it on both pages: ${changed[0].trim()}`);
}

/* A free room: free.gen, an instrument per seat and nothing composed. A
 * plays its seat quantized and, while the room plays, picks another graph
 * for it from the picker beside the seat. The pick is an edit of the
 * document, applied at the next bar on both pages: afterwards both worklets
 * have the seat on the new graph, B's seat list says so, and the two tapes
 * are one tape and genwav's under the same keys and the same edit.
 */
async function pickTogether (pages)
{
    const [A, B] = pages;

    for (const { label, page } of pages)
    {
        await page.goto(`${url}&room=jamfree&name=${label}` +
                        `&piece=${FREE_PIECE}`);
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

    const channelOf = (who, name) => who.page.evaluate(
        (n) => window.jam.instruments().find((i) => i.name === n)?.channel,
        name);
    const one = await channelOf(A, 'one');
    const two = await channelOf(B, 'two');

    for (const [who, seat] of [[A, one], [B, two]])
    {
        await who.page.evaluate((s) =>
        {
            window.jam.seat(s);
            window.jam.mode('quantised');
        }, seat);
        await who.page.waitForFunction((s) => window.jam.seatNow() === s,
                                       seat, { timeout: 5000 })
            .catch(() => fail(`${who.label} did not get seat ${seat}`));
    }

    await A.page.waitForFunction(
        () => document.getElementById('instrument').value === 'rhodes.dsp',
        null, { timeout: 5000 })
        .catch(() => fail(`${A.label}'s picker does not show its seat's ` +
                          'rhodes.dsp'));

    await A.page.evaluate(() => window.jam.play());

    const t0 = Date.now();
    const at = (ms) => new Promise((r) =>
        setTimeout(r, Math.max(0, t0 + ms - Date.now())));

    for (let k = 0; k < 8; k++)
    {
        if (k === 3)
            await A.page.selectOption('#instrument', 'dxbell.dsp');

        await at(2000 + k * 700);
        await A.page.evaluate((k) => window.jam.press(60 + k, 90), k);
        await at(2000 + k * 700 + 300);
        await A.page.evaluate((k) => window.jam.release(60 + k), k);
    }

    const landed = (page) => page.waitForFunction(
        () => window.jam.edits() >= 1 &&
              window.jam.instruments().find((i) => i.name === 'one')?.dsp ===
                  'dxbell.dsp',
        null, { timeout: 10000 }).then(() => true, () => false);

    for (const { label, page } of pages)
        if (await landed(page))
            ok(`${label} plays seat one on dxbell.dsp`);
        else
            fail(`${label} did not put seat one on dxbell.dsp: ` +
                 JSON.stringify(await page.evaluate(
                     () => window.jam.instruments())));

    const label = await B.page.$eval(
        '#seat', (s, c) => [...s.options].find((o) => o.value === String(c))
            ?.textContent ?? '', one);

    if (/^one: FM Bell/.test(label))
        ok(`${B.label}'s seat list says "${label}"`);
    else
        fail(`${B.label}'s seat list says "${label}" for seat one`);

    const theirs = await B.page.$eval('#instrument',
                                      (s) => [s.value, s.disabled]);

    if (theirs[0] === 'juno.dsp' && !theirs[1])
        ok(`${B.label}'s picker shows its own seat's juno.dsp`);
    else
        fail(`${B.label}'s picker shows ${theirs[0]}` +
             (theirs[1] ? ', disabled' : ''));

    await at(9000);
    await A.page.evaluate(() => window.jam.stop());
    await at(12000);

    const results = [];

    for (const { label, page } of pages)
        results.push({ label, ...(await page.evaluate(() => ({
            tape: window.jam.tape(),
            sent: window.jam.sent(),
        }))) });

    const sent = results.flatMap((r) => r.sent).filter((c) => c.at >= 0);
    const stopAt = sent.find((c) => c.op === 'stop')?.at;

    if (stopAt === undefined || !sent.some((c) => c.type === 'edit'))
    {
        fail('the free room sent no stop or no edit');
        return;
    }

    const tapes = results.map((r) => tapeBefore(r.tape, stopAt));
    const want = reference(FREE_PIECE, nodeBuild,
                           { commands: sent, knobs: {}, stopAt });

    if (tapes[0] === tapes[1] && tapes[0] === want)
        ok('the free room is one tape and genwav\'s across the pick: ' +
           `${tapes[0].split('\n').length - 1} events`);
    else
        fail(tapes[0] !== tapes[1]
             ? `the free room's tapes differ: ` +
               `${firstDifference(tapes[0], tapes[1])}`
             : `the free room differs from genwav: ` +
               `${firstDifference(want, tapes[0])}`);
}

/* Keys into a piece, from both pages: hands.gen, which composes nothing
 * but what it is played. One page sits on the quantizer's channel and
 * plays quantised, the other on an arpeggiator's and plays a bar ahead. A
 * direct key into a piece would reach the two peers' pieces at two
 * different points, which is what the other two modes are for -- so the
 * two tapes have to be one tape, and genwav's under the same keys.
 */
async function playTogether (pages)
{
    const [A, B] = pages;

    for (const { label, page } of pages)
    {
        await page.goto(`${url}&room=jamhands&name=${label}` +
                        `&piece=${HANDS_PIECE}`);
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

    /* The quantizer takes input on hands.gen's second channel, the first
       arpeggiator on its first. */
    const seats = [[A, 1, 'quantised'], [B, 0, 'ahead']];

    for (const [who, seat, mode] of seats)
    {
        await who.page.evaluate(([s, m]) =>
        {
            window.jam.seat(s);
            window.jam.mode(m);
        }, [seat, mode]);
        await who.page.waitForFunction((s) => window.jam.seatNow() === s,
                                       seat, { timeout: 5000 })
            .catch(() => fail(`${who.label} did not get seat ${seat}`));
    }

    await A.page.evaluate(() => window.jam.play());

    const t0 = Date.now();
    const at = (ms) => new Promise((r) =>
        setTimeout(r, Math.max(0, t0 + ms - Date.now())));

    for (let k = 0; k < 6; k++)
    {
        await at(2000 + k * 1100);
        await A.page.evaluate((k) =>
        {
            for (const n of [53, 56, 60])
                window.jam.press(n + (k % 3), 90);
        }, k);
        await B.page.evaluate((k) => window.jam.press(65 + k, 90), k);

        await at(2000 + k * 1100 + 450);
        await A.page.evaluate((k) =>
        {
            for (const n of [53, 56, 60])
                window.jam.release(n + (k % 3));
        }, k);
        await B.page.evaluate((k) => window.jam.release(65 + k), k);
    }

    await at(PLAY_SECONDS * 1000);
    await A.page.evaluate(() => window.jam.stop());
    await at(PLAY_SECONDS * 1000 + 3000);

    const results = [];

    for (const { label, page } of pages)
        results.push({ label, ...(await page.evaluate(() => ({
            tape: window.jam.tape(),
            sent: window.jam.sent(),
            late: window.jam.late(),
        }))) });

    const sent = results.flatMap((r) => r.sent).filter((c) => c.at >= 0);
    const keys = sent.filter((c) => c.type === 'note');
    const stopAt = sent.find((c) => c.op === 'stop')?.at;

    const quantised = keys.filter((c) => c.mode === 'quantised');
    const ahead = keys.filter((c) => c.mode === 'ahead');

    if (quantised.length === 0 || ahead.length === 0)
    {
        fail(`${quantised.length} quantised and ${ahead.length} bar-ahead ` +
             'keys went out; both are wanted');
        return;
    }

    /* A sixteenth at 120 is an eighth of a second, and the grid starts at
       transport zero. */
    const offGrid = quantised.filter(
        (c) => Math.abs(c.at * 8 - Math.round(c.at * 8)) > 1e-6);

    if (offGrid.length === 0)
        ok(`${quantised.length} quantised keys, every one on a sixteenth`);
    else
        fail(`quantised keys off the grid: ` +
             offGrid.map((c) => c.at.toFixed(4)).join(', '));

    ok(`${ahead.length} keys a bar ahead`);

    if (stopAt === undefined)
    {
        fail('no stop was sent in the room played into');
        return;
    }

    const tapes = results.map((r) => tapeBefore(r.tape, stopAt));

    if (tapes[0] === tapes[1])
        ok('keys played into the piece from two pages are one tape: ' +
           `${tapes[0].split('\n').length - 1} events`);
    else
        fail(`the played tapes differ: ` +
             `${firstDifference(tapes[0], tapes[1])}`);

    const want = reference(HANDS_PIECE, nodeBuild,
                           { commands: sent, knobs: {}, stopAt });

    if (tapes[0] === want)
        ok('and it is the tape genwav delivers under the same keys');
    else
        fail(`the played tape differs from genwav's: ` +
             `${firstDifference(want, tapes[0])}`);

    for (const r of results)
        if (r.late.worklet !== 0 || r.late.seen !== 0)
            fail(`${r.label} applied ${r.late.worklet} late in the room ` +
                 'played into');
}

/* A room on cloud.gen, whose cloud.dsp plays two of dsp/samples/ through
   osc::sample. A wav the page did not hand over is a line in the log and
   a voice that plays nothing. */
async function sampleTogether (pages)
{
    const [A] = pages;

    for (const { label, page } of pages)
    {
        await page.goto(`${url}&room=jamkit&name=${label}` +
                        `&piece=${SAMPLE_PIECE}`);
        await page.waitForFunction(
            () => !document.getElementById('roompanel').hidden,
            null, { timeout: 15000 });
        await page.click('#start');
        await page.waitForFunction(() => window.jam.ready(), null,
                                   { timeout: 20000 });
    }

    await A.page.evaluate(() => window.jam.play());
    await new Promise((r) => setTimeout(r, SAMPLE_SECONDS * 1000));
    await A.page.evaluate(() => window.jam.stop());

    for (const { label, page } of pages)
    {
        const missing = (await page.evaluate(
            () => document.getElementById('log').textContent))
            .split('\n').filter((line) => /osc::sample/.test(line));

        if (missing.length === 0)
            ok(`${label} loaded ${SAMPLE_PIECE}'s samples`);
        else
            fail(`${label}: ${missing.join('; ')}`);
    }
}

/* A stage's parameter, typed into the popover beside its box.
 *
 * The panel is the module's description of the stage (src/StagePanel.cpp)
 * drawn by panel.js, which is the same renderer every other panel on this
 * page uses; what is pressed here is an ordinary number box in it. What the
 * rows say is checked where the module is -- wasm/web/panelcheck.mjs -- and
 * what is checked here is that typing in one reaches the room.
 *
 * True if something was actually set. A piece whose first stage has nothing
 * to type into would leave the caller asserting nothing, which is the one
 * outcome a harness must not report as a pass.
 */
async function editParam (peer)
{
    const handle = await peer.page.evaluate(
        () => window.jam.handleOf(0, 0));

    if (!handle || handle.x < 0)
        return false;

    await peer.page.locator('#composerscroll').scrollIntoViewIfNeeded();

    const box = await peer.page.$eval('#composer', (c) =>
    {
        const r = c.getBoundingClientRect();

        return { x: r.x, y: r.y };
    });

    await peer.page.mouse.click(box.x + handle.x, box.y + handle.y);
    await peer.page.waitForFunction(
        () => !document.getElementById('composerparams').hidden,
        null, { timeout: 15000 }).catch(() => {});

    const input = await peer.page.$(
        '#composerparams .panelrow input[type="number"]:not([disabled])');

    if (input === null)
        return false;

    const row = await input.evaluate((e) => e.closest('.panelrow')
                                             .dataset.row);
    const want = String(Number(await input.inputValue()) + 1);

    await input.fill(want);
    await input.press('Enter');

    /* The description coming back with the new number in it, which is this
       peer's own module having taken the edit off the room rather than off
       the box (docs/JAM.md: the page is the nearest peer, not a privileged
       one). */
    await peer.page.waitForFunction(
        ([id, value]) => document.querySelector(
            `#composerparams .panelrow[data-row="${id}"] ` +
            'input[type="number"]')?.value === value,
        [row, want], { timeout: 15000 }).catch(() => {});

    return true;
}

/*
 * The .dsp canvas, in a room.
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

    /* The row carries its own identity, which for a node's panel is the
       arg's name; the control inside it is just a control. */
    const arg = await input.evaluate(
        (i) => i.closest('.panelrow').dataset.row);
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
       panel on the node. The samples themselves are gated headlessly in
       nodecheck; what is under test here is that a person can ask for
       one, and take it away again. */
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

    /* The canvas's origin again, and not the one read before the edit
       above: setting a value rewrites the file, which rebuilds the graph
       and redraws the params panel under the canvas -- so a panel that
       came out a different height has moved everything above it, and a
       right-click aimed with the old origin lands beside the port rather
       than on it. Ports are a few pixels across; the miss is silent and
       looks like a menu that offered nothing. */
    const canvasAt = await A.page.$eval('#nodecanvas', (c) =>
    {
        const r = c.getBoundingClientRect();

        return { x: r.x, y: r.y };
    });

    await A.page.mouse.click(canvasAt.x + port.x, canvasAt.y + port.y,
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
/* The document rather than the tiled layout. Both are the page -- panes.js
   adopts what is in the markup and puts it back, and below 60em or under a
   finger the tiled one is not offered at all -- and what is under test
   here is two peers agreeing on a piece, not where either of them puts
   the piece on screen. panecheck.mjs is the harness for that. */
const url = `http://127.0.0.1:${site.address().port}/jam.html?panes=0`;

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

        await page.goto(`${url}&room=jamtest&name=${label}&piece=${PIECE}`);
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
       not seen yet, so B's start waits for the update before it loads,
       and whatever arrives while it waits has to survive the wait rather
       than be cleared by the load or the arm. A comment, so the piece
       composes exactly as it did. */
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

    /* An edit, applied while it plays: the wildcard chain always plays,
       and louder. */
    await at(10000);
    {
        const edited = await A.page.evaluate(() =>
        {
            const name = window.jam.piece();
            const was = window.jam.file(name);
            const next = was.replace('prob = 0.6; hold = 5 s; vel = 50;',
                                     'prob = 1; hold = 5 s; vel = 90;');

            if (next === was)
                return false;

            window.jam.setFile(name, next);
            window.jam.apply();

            return true;
        });

        if (!edited)
            fail('the edit found nothing to change in the piece');
    }

    await at(12000);
    await B.page.evaluate(() => window.jam.tempo(100));
    await at(16000);
    await A.page.evaluate(() => window.jam.knob(0, 0.05));

    /* The late joiner. */
    await at(JOIN_AT * 1000);

    let C = null;

    {
        const label = 'joiner';
        const page = await browsers[0].newPage();

        page.on('pageerror', (e) => errors.push(`${label}: ${e.message}`));
        page.on('console', (m) =>
        {
            if (m.type() === 'error')
                errors.push(`${label} console: ${m.text()}`);
        });

        await page.goto(`${url}&room=jamtest&name=${label}&piece=${PIECE}`);
        await page.waitForFunction(
            () => !document.getElementById('roompanel').hidden,
            null, { timeout: 15000 });
        await page.click('#start');

        try
        {
            await page.waitForFunction(
                () => window.jam.ready() && !window.jam.catching() &&
                      window.jam.probe().running,
                null, { timeout: (JOINER_KNOB_AT - JOIN_AT) * 1000 });
            ok(`a third page joined ${((Date.now() - t0) / 1000 - JOIN_AT)
                .toFixed(1)} s after arriving, caught up with the room`);
            C = { label, page };
        }
        catch
        {
            fail(`the late joiner never caught up -- ${await why(page)}`);
            await page.close();
        }
    }

    await at(JOINER_KNOB_AT * 1000);

    if (C !== null)
        await C.page.evaluate(() => window.jam.knob(0, 0.6));

    await at(SECONDS * 1000);
    await A.page.evaluate(() => window.jam.stop());
    await at(SECONDS * 1000 + 3000);

    /* What each delivered, and what each saw. */
    const results = [];

    for (const { label, page } of C === null ? pages : [...pages, C])
        results.push({ label, ...(await page.evaluate(() => ({
            tape: window.jam.tape(),
            late: window.jam.late(),
            sent: window.jam.sent(),
            edits: window.jam.edits(),
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
        .filter((c) => (c.type === 'knob' || c.type === 'transport' ||
                        c.type === 'edit') && c.at >= 0)
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

        if (C !== null && tapes[2] === tapes[0])
            ok('and the late joiner\'s is the same tape, from the top');
        else if (C !== null)
            fail(`the late joiner's tape differs: ` +
                 `${firstDifference(tapes[0], tapes[2])}`);

        /* The knob names, for genwav, by the index a command names one
           by -- which is the module's numbering over every knob the piece
           declared, hidden ones included, and not the position of the
           slider on the page. Each row carries both: its id is the number
           and `data-knob' is the word the .gen writes. */
        const knobs = await A.page.evaluate(() => Object.fromEntries(
            [...document.querySelectorAll('#knobs .panelrow')].map(
                (line) => [line.dataset.row, line.dataset.knob])));
        const want = reference(PIECE, nodeBuild,
                               { commands: sent, knobs, stopAt });

        if (tapes[0] === want)
            ok('and it is the tape genwav delivers under the same commands');
        else
            fail(`chromium's tape differs from genwav's: ` +
                 `${firstDifference(want, tapes[0])}`);

        /* And the edit is heard: without it, genwav composes something
           else. */
        const unedited = reference(PIECE, nodeBuild, {
            commands: sent.filter((c) => c.type !== 'edit'), knobs, stopAt });

        if (unedited !== want)
            ok('and not the tape of the run nobody edited');
        else
            fail('the edit made no difference to the tape');
    }

    for (const r of results)
        if (r.edits === 1)
            ok(`${r.label} applied the edit`);
        else
            fail(`${r.label} applied ${r.edits} edits, not 1`);

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
       check for nothing. */
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

    await C?.page.close();

    /* ---- and now somebody paints on a Life board ---- */

    await paintTogether(pages);

    /* ---- and edits an instrument on the canvas ---- */

    await editTogether(pages);

    /* ---- and plays into a piece from both ---- */

    await playTogether(pages);

    /* ---- and plays recordings ---- */

    await sampleTogether(pages);

    /* ---- and clicks a cell in a Sequencer ---- */

    await sequenceTogether(pages);

    /* ---- and picks an instrument for a seat while it plays ---- */

    await pickTogether(pages);

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
