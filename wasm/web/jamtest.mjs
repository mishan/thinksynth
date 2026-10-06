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
 * Then a room on free.gen, whose seat one gets another graph from the
 * picker, stopped and while playing, on both pages; and one on acetate.gen,
 * where a graph without the chanarg a chain rides is refused.
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
 * Then the room's piece switched from inside it: twice at once, stopped
 * and while it plays; under a page whose document has gone past the
 * relay's Play of it, or that hears a Play already replaced; after a knob
 * of the piece switched from, and under an editor tab; to village.gen
 * while it plays, which a late joiner has to catch up with, and to
 * colony.gen while it is stopped. Both pages end up with the new piece's
 * files and nothing else, the same tabs and node editor menu, and genwav's
 * tape for it. And a .gen pasted over the room's naming other graphs: the
 * tabs and the menu follow the text, and a graph it names that the room
 * lacks comes in at the Play.
 *
 * Then the two pages talk: a line each way through the room's chat, and
 * a Play from one reported in the other's feed.
 *
 * Last, one page makes an account in the account dialog and logs in with
 * its key on a reload, and the other joins as a guest: each shows the
 * handle as it is and the guest marked as one. A Chromium page served as
 * localhost, the passkeys' RP ID, makes an account with a passkey from a
 * virtual authenticator and logs back in with it on a reload; Firefox has
 * no virtual authenticator Playwright can drive, so it does not.
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

/* What a room on PIECE is switched to while it plays; amb01.dsp is in
   both, and the rest only in this. */
const SWITCH_PIECE = 'village.gen';
const SWITCH_SECONDS = 10;

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
    {
        /* Where B skipped an edit, its log says which way. */
        const logged = (await B.page.evaluate(
            () => document.getElementById('log').textContent))
            .split('\n').filter(Boolean).slice(-12);

        fail(`the click is not the snare's cells in ${pieceFile}: ` +
             `${JSON.stringify(changed)}; ${B.label}'s log ends\n      ` +
             logged.join('\n      '));
    }
    else
        ok(`and ${pieceFile} carries it on both pages: ${changed[0].trim()}`);
}

/* Both pages into a room on `piece', started, and the mesh up. */
async function enter (pages, room, piece)
{
    for (const { label, page } of pages)
    {
        await page.goto(`${url}&room=${room}&name=${label}&piece=${piece}`);
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
}

/* `who' into the seat instrument `name' is on. */
async function sit (who, name)
{
    const seat = await who.page.evaluate(
        (n) => window.jam.instruments().find((i) => i.name === n)?.channel,
        name);

    await who.page.evaluate((s) => window.jam.seat(s), seat);
    await who.page.waitForFunction((s) => window.jam.seatNow() === s,
                                   seat, { timeout: 5000 })
        .catch(() => fail(`${who.label} did not get ${name}'s seat`));

    return seat;
}

/* Whether a page has instrument `name' on graph `dsp'. */
const playsOn = (page, name, dsp) => page.waitForFunction(
    ([n, d]) => window.jam.instruments().find((i) => i.name === n)?.dsp === d,
    [name, dsp], { timeout: 10000 }).then(() => true, () => false);

/* A free room: free.gen, an instrument per seat and nothing composed.
 *
 * Stopped, A picks a graph for its seat from the picker: both pages put it
 * on at once, and A's picker still shows it after a few seconds of pings.
 * Then A plays the seat quantized and picks again while the room plays:
 * a pick lands at the next bar on both pages, B's seat list says so, and
 * the two tapes are one tape and genwav's under the same keys and pick.
 */
async function pickTogether (pages)
{
    const [A, B] = pages;

    await enter(pages, 'jamfree', FREE_PIECE);

    const one = await sit(A, 'one');

    await sit(B, 'two');
    await A.page.evaluate(() => window.jam.mode('quantised'));

    await A.page.selectOption('#instrument', 'pluck.dsp');

    for (const { label, page } of pages)
        if (await playsOn(page, 'one', 'pluck.dsp'))
            ok(`stopped, ${label} put seat one on pluck.dsp at once`);
        else
            fail(`stopped, ${label} did not put seat one on pluck.dsp`);

    await new Promise((r) => setTimeout(r, 3000));

    const shown = await A.page.$eval('#instrument', (s) => s.value);

    if (shown === 'pluck.dsp')
        ok(`${A.label}'s picker still shows pluck.dsp three seconds on`);
    else
        fail(`${A.label}'s picker went back to ${shown}`);

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

    for (const { label, page } of pages)
        if (await playsOn(page, 'one', 'dxbell.dsp'))
            ok(`playing, ${label} put seat one on dxbell.dsp`);
        else
            fail(`playing, ${label} did not put seat one on dxbell.dsp: ` +
                 JSON.stringify(await page.evaluate(
                     () => window.jam.instruments())));

    const label = await B.page.$eval(
        '#seat', (s, c) => [...s.options].find((o) => o.value === String(c))
            ?.textContent ?? '', one);

    if (/^one: FM Bell \(channel 1\)$/.test(label))
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

    if (stopAt === undefined || !sent.some((c) => c.type === 'pick'))
    {
        fail('the free room sent no stop or no pick while playing');
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

/* A pick that would leave a piece that does not load: acetate.gen rides
 * its bass's `cutoff' from a chain, and dxbell.dsp declares none. Refused
 * on the page that picked, with the reason, and nothing changes: not the
 * document, not either page's instrument, and the picker goes back. */
async function pickRefused (pages)
{
    const [A] = pages;

    await enter(pages, 'jampickrefused', 'acetate.gen');
    await sit(A, 'bass');

    const was = await A.page.evaluate(() => window.jam.file('acetate.gen'));

    await A.page.selectOption('#instrument', 'dxbell.dsp');

    const said = await A.page.waitForFunction(
        () => /declares no chanarg called 'cutoff'/.test(
            document.getElementById('status').textContent),
        null, { timeout: 5000 }).then(() => true, () => false);

    if (said)
        ok(`${A.label} refused dxbell.dsp for acetate.gen's bass, and said ` +
           'why');
    else
        fail(`${A.label} says "${await A.page.$eval(
            '#status', (e) => e.textContent)}" for a pick onto a graph ` +
             'with no cutoff');

    const after = await Promise.all(pages.map(({ page }) => page.evaluate(
        () => [window.jam.file('acetate.gen'),
               window.jam.instruments().find((i) => i.name === 'bass')?.dsp,
               document.getElementById('instrument').value])));

    if (after.every(([text, dsp]) => text === was && dsp === 'bass.dsp') &&
        after[0][2] === 'bass.dsp')
        ok('and the document, both pages and the picker are as they were');
    else
        fail(`after the refused pick: ${JSON.stringify(
            after.map(([text, dsp, shown]) =>
                [text === was ? 'document as was' : 'document changed',
                 dsp, shown]))}`);
}

/* What a page has in its document, which of those it has tabs for and
   which its node editor offers, as one string to compare. */
const filesShown = () => JSON.stringify([window.jam.files(),
                                         window.jam.tabs(),
                                         window.jam.nodeFiles()]);

/* Whether `page' comes to show `want' ([files, tabs, node files]): null if
   it does, what it shows instead if not. */
async function shows (page, want)
{
    const wanted = JSON.stringify(want);
    const came = await page.waitForFunction(
        `(${filesShown})() === ${JSON.stringify(wanted)}`, null,
        { timeout: 10000 }).then(() => true, () => false);

    return came ? null : await page.evaluate(filesShown);
}

/* A from `#piece' to `name', saying yes when asked. */
async function switchTo (A, name)
{
    A.page.once('dialog', (d) => d.accept());
    await A.page.selectOption('#piece', name);
}

/* A shipped file's text, as the page fetches it. */
const shipped = (page, path) => page.evaluate(
    (p) => fetch(p).then((r) => r.text()), path);

/* What can go wrong around a switch, stopped, in a room on airports.gen.
 *
 * A switches to colony.gen, which has airports.gen's density knob, and
 * lets go of that knob while airports.gen is still what it has loaded:
 * the knob is airports.gen's, and colony.gen must not get it.
 *
 * Then both pages switch at once, to two different pieces: both documents
 * end up holding one of them and its graphs, the same one, and nothing of
 * the other.
 *
 * Then B edits amb01.dsp, goes back to the .gen tab, and A switches to
 * ebb.gen, which has amb01.dsp too. B opens that tab again and types into
 * it: the line goes in at the top of ebb.gen's amb01.dsp, as shipped, and
 * not at an offset into the text B's tab last showed. */
async function switchRaces (pages)
{
    const [A, B] = pages;

    await enter(pages, 'jamswitchraces', PIECE);
    await switchTo(A, PAINT_PIECE);

    for (const { page } of pages)
        await page.waitForFunction((p) => window.jam.piece() === p,
                                   PAINT_PIECE, { timeout: 10000 })
            .catch(() => {});

    const colony = await shipped(A.page, `gen/${PAINT_PIECE}`);

    await A.page.$eval('#knobs .panelrow[data-knob="density"] ' +
                       'input[type="range"]', (input) =>
    {
        input.value = 0.3;
        input.dispatchEvent(new Event('change'));
    });
    await new Promise((r) => setTimeout(r, 2000));

    const after = await Promise.all(pages.map(({ page }) =>
        page.evaluate((p) => window.jam.file(p), PAINT_PIECE)));

    if (after.every((t) => t === colony))
        ok(`a knob of ${PIECE} let go of after the switch is not written ` +
           `into ${PAINT_PIECE}`);
    else
        fail(`${PAINT_PIECE} changed under a knob of ${PIECE}: ` +
             firstDifference(colony, after[0] === colony ? after[1]
                                                         : after[0]));

    const both = [PIECE, SWITCH_PIECE];

    await Promise.all([switchTo(A, both[0]), switchTo(B, both[1])]);
    await new Promise((r) => setTimeout(r, 2000));

    const docs = await Promise.all(pages.map(({ page }) => page.evaluate(
        () => ({ piece: window.jam.piece(),
                 files: Object.fromEntries(window.jam.files().map(
                     (n) => [n, window.jam.file(n)])) }))));
    const piece = docs[0].piece;
    const want = piece === null ? null : await (async () =>
    {
        const gen = await shipped(A.page, `gen/${piece}`);
        const out = { [piece]: gen };

        for (const m of gen.matchAll(/\b(?:dsp|effect)\s+"([^"]+)"/g))
            out[m[1]] = await shipped(A.page, `dsp/${m[1]}`);

        return JSON.stringify(Object.fromEntries(
            Object.entries(out).sort(([a], [b]) => a.localeCompare(b))));
    })();

    if (both.includes(piece) &&
        docs.every((d) => d.piece === piece &&
                          JSON.stringify(d.files) === want))
        ok(`two switches at once leave both pages with ${piece} as ` +
           'shipped, and nothing of the other');
    else
        fail(`after two switches at once: ${JSON.stringify(docs.map(
            (d) => [d.piece, Object.keys(d.files)]))}`);

    await B.page.click('#tabs button:text-is("amb01.dsp")');
    await B.page.click('.cm-content');
    await B.page.keyboard.press('Control+Home');
    await B.page.keyboard.type('# a line of B\'s\n');
    await B.page.evaluate(() =>
        [...document.querySelectorAll('#tabs button')]
            .find((b) => b.textContent.endsWith('.gen')).click());

    await switchTo(A, 'ebb.gen');
    await B.page.waitForFunction(() => window.jam.piece() === 'ebb.gen',
                                 null, { timeout: 10000 }).catch(() => {});
    await B.page.click('#tabs button:text-is("amb01.dsp")');
    await B.page.click('.cm-content');
    await B.page.keyboard.press('Control+Home');
    await B.page.keyboard.type('# another\n');
    await new Promise((r) => setTimeout(r, 1000));

    const amb = '# another\n' + await shipped(A.page, 'dsp/amb01.dsp');
    const typed = await Promise.all(pages.map(({ page }) =>
        page.evaluate(() => window.jam.file('amb01.dsp'))));

    if (typed.every((t) => t === amb))
        ok(`${B.label}'s tab on amb01.dsp, edited before the switch and ` +
           'shown again after it, types into the new text');
    else
        fail(`amb01.dsp after typing into a tab edited before the ` +
             `switch: ${firstDifference(amb, typed.find((t) => t !== amb))}`);
}

/* The room's piece switched from inside it.
 *
 * Playing, A switches airports.gen to village.gen: a Play of the new
 * piece from the top on both pages, and a third page joining after it
 * catches up with village.gen, not airports.gen. All three tapes are
 * genwav's for it, and all three documents hold village.gen and its four
 * graphs and nothing of airports.gen's -- which the tabs and the node
 * editor's menu say too.
 * Then, stopped, A switches to colony.gen and B's Play plays it. */
async function switchTogether (pages, browser)
{
    const [A, B] = pages;

    await enter(pages, 'jamswitch', PIECE);
    await A.page.evaluate(() => window.jam.play());
    await new Promise((r) => setTimeout(r, 3000));

    const t0 = Date.now();
    const at = (ms) => new Promise((r) =>
        setTimeout(r, Math.max(0, t0 + ms - Date.now())));

    await switchTo(A, SWITCH_PIECE);

    const graphs = ['amb01.dsp', 'bdshaped.dsp', 'hat0.dsp', 'ts1.dsp'];
    const village = [[...graphs, SWITCH_PIECE].sort(),
                     [...graphs, SWITCH_PIECE].sort(), graphs];

    for (const { label, page } of pages)
    {
        const loaded = await page.waitForFunction(
            () => window.jam.instruments().some((i) => i.dsp === 'hat0.dsp'),
            null, { timeout: 10000 }).then(() => true, () => false);

        if (!loaded)
            fail(`${label} never loaded ${SWITCH_PIECE} after the switch`);
    }

    /* The late joiner, after the switch. */
    await at(3000);

    let C = null;

    {
        const label = 'switchjoiner';
        const page = await browser.newPage();

        await page.goto(`${url}&room=jamswitch&name=${label}&piece=${PIECE}`);
        await page.waitForFunction(
            () => !document.getElementById('roompanel').hidden,
            null, { timeout: 15000 });
        await page.click('#start');

        /* Fifteen seconds, not five: Start opens an audio context and
           waits for four clock samples of it and of the relay before the
           catch-up begins, then loads the switched piece's document and
           steps through the run -- and on a loaded runner the joiner has
           been seen still catching up at five, its clocks long ready. */
        if (await page.waitForFunction(
                () => window.jam.ready() && !window.jam.catching() &&
                      window.jam.probe().running,
                null, { timeout: 15000 }).then(() => true, () => false))
            C = { label, page };
        else
        {
            fail(`the joiner after the switch never caught up -- ` +
                 `${await why(page)}`);
            await page.close();
        }
    }

    await at(SWITCH_SECONDS * 1000);
    await A.page.evaluate(() => window.jam.stop());
    await at(SWITCH_SECONDS * 1000 + 3000);

    const everyone = C === null ? pages : [...pages, C];
    const results = [];

    for (const { label, page } of everyone)
        results.push({ label, ...(await page.evaluate(() => ({
            tape: window.jam.tape(),
            sent: window.jam.sent(),
            piece: window.jam.piece(),
            shown: document.getElementById('piece').value,
        }))) });

    const stop = results.flatMap((r) => r.sent)
        .find((c) => c.op === 'stop' && c.at >= 0);

    if (stop === undefined)
    {
        fail('nothing stopped the switched room');
        await C?.page.close();
        return;
    }

    const tapes = results.map((r) => tapeBefore(r.tape, stop.at));
    const want = reference(SWITCH_PIECE, nodeBuild,
                           { commands: [stop], stopAt: stop.at });

    for (let i = 0; i < results.length; i++)
        if (tapes[i] === want)
            ok(`${results[i].label}'s tape after the switch is genwav's ` +
               `for ${SWITCH_PIECE}: ${want.split('\n').length - 1} events`);
        else
            fail(`${results[i].label}'s tape after the switch differs from ` +
                 `genwav's for ${SWITCH_PIECE}: ` +
                 `${firstDifference(want, tapes[i])}`);

    for (const [i, { label, page }] of everyone.entries())
    {
        const wrong = await shows(page, village);

        if (wrong === null && results[i].piece === SWITCH_PIECE &&
            results[i].shown === SWITCH_PIECE)
            ok(`${label} has ${SWITCH_PIECE}'s files only, in its tabs ` +
               'and its node editor too');
        else
            fail(`${label} after the switch: piece ${results[i].piece}, ` +
                 `switcher ${results[i].shown}, files, tabs and node ` +
                 `files ${wrong}`);
    }

    await C?.page.close();

    /* And stopped: the document now, the piece at the next Play, which
       here is B's. */
    await switchTo(A, PAINT_PIECE);

    const wrong = await shows(B.page, [['amb01.dsp', 'colony.gen', 'ts1.dsp'],
                                       ['amb01.dsp', 'colony.gen', 'ts1.dsp'],
                                       ['amb01.dsp', 'ts1.dsp']]);

    if (wrong === null)
        ok(`stopped, ${A.label}'s switch to ${PAINT_PIECE} reached ` +
           B.label);
    else
        fail(`stopped, ${B.label} shows ${wrong} after the switch to ` +
             PAINT_PIECE);

    await B.page.evaluate(() => window.jam.play());
    await new Promise((r) => setTimeout(r, 6000));
    await B.page.evaluate(() => window.jam.stop());
    await new Promise((r) => setTimeout(r, 3000));

    const after = [];

    for (const { page } of pages)
        after.push(await page.evaluate(() => ({ tape: window.jam.tape(),
                                                sent: window.jam.sent() })));

    const stopped = after[1].sent.findLast((c) => c.op === 'stop' &&
                                                   c.at >= 0);
    const theirs = after.map((r) => tapeBefore(r.tape, stopped.at));
    const colony = reference(PAINT_PIECE, nodeBuild,
                             { commands: [stopped], stopAt: stopped.at });

    if (theirs[0] === colony && theirs[1] === colony)
        ok(`and ${B.label}'s Play played it on both pages, genwav's tape`);
    else
        fail(`after the stopped switch, ${PAINT_PIECE}'s tape differs: ` +
             firstDifference(colony, theirs[theirs[0] === colony ? 1 : 0]));
}

/* Two switches at once while the room plays: A to ebb.gen and B to
 * village.gen. The relay plays the last of them from the top, and both
 * pages end up on it -- one tape, genwav's for that piece -- with nothing
 * loaded late on either: a page whose document went past a Play's
 * revision loads the relay's copy of it rather than waiting it out. */
async function switchRacePlaying (pages)
{
    const [A, B] = pages;

    await enter(pages, 'jamswitchplaying', PIECE);
    await A.page.evaluate(() => window.jam.play());
    await new Promise((r) => setTimeout(r, 3000));

    const lateWas = await Promise.all(pages.map(({ page }) =>
        page.evaluate(() => window.jam.late().seen)));

    await Promise.all([switchTo(A, 'ebb.gen'), switchTo(B, SWITCH_PIECE)]);
    await new Promise((r) => setTimeout(r, 8000));
    await A.page.evaluate(() => window.jam.stop());
    await new Promise((r) => setTimeout(r, 3000));

    const results = [];

    for (const { label, page } of pages)
        results.push({ label, ...(await page.evaluate(() => ({
            tape: window.jam.tape(),
            sent: window.jam.sent(),
            late: window.jam.late(),
            piece: window.jam.piece(),
        }))) });

    const piece = results[0].piece;
    const stop = results[0].sent.findLast((c) => c.op === 'stop' &&
                                                 c.at >= 0);

    if (!['ebb.gen', SWITCH_PIECE].includes(piece) ||
        results[1].piece !== piece || stop === undefined || stop.at < 0)
    {
        fail(`after two switches while playing: ${results.map(
            (r) => r.piece).join(', ')}, stop ${stop?.at}`);
        return;
    }

    const tapes = results.map((r) => tapeBefore(r.tape, stop.at));
    const want = reference(piece, nodeBuild,
                           { commands: [stop], stopAt: stop.at });

    if (tapes[0] === want && tapes[1] === want)
        ok(`two switches at once while playing: both pages play ${piece} ` +
           `from the top, genwav's tape, ${want.split('\n').length - 1} ` +
           'events');
    else
        fail(`two switches at once while playing, ${piece}: ` +
             firstDifference(want, tapes[tapes[0] === want ? 1 : 0]));

    for (const [i, r] of results.entries())
        if (r.late.seen === lateWas[i] && r.late.worklet === 0)
            ok(`and ${r.label} loaded nothing late`);
        else
            fail(`${r.label} counted ${r.late.seen - lateWas[i]} late ` +
                 `loads and ${r.late.worklet} late commands: ` +
                 r.late.page.slice(-3).map((c) =>
                     `${c.from}#${c.seq} ${c.op ?? c.type}`).join(', '));
}

/* A Play whose revision a page's document has gone past by the time the
 * page gets to it, which B's room socket is held back to arrange: what
 * the relay says to B waits until this lets it go, while B's document
 * goes on syncing.
 *
 * Playing colony.gen, A switches to airports.gen and, before B hears the
 * relay's Play of it, edits a chain in airports.gen without applying it.
 * B's document has gone past the Play's revision; B loads the relay's
 * copy of it, so both tapes are genwav's for airports.gen as shipped and
 * not the edited one's, and nothing is late.
 *
 * Then two switches far enough apart for the relay to play each, held
 * back from B until both Plays are there: the first has been replaced by
 * the time B gets to it, which is no late load, and both pages end on the
 * second, genwav's tape again. */
async function passedTogether (pages)
{
    const [A, B] = pages;
    let hold = false;
    const held = [];

    await B.page.routeWebSocket(/\/room\//, (ws) =>
    {
        const server = ws.connectToServer();

        server.onMessage((m) => (hold ? held.push([ws, m]) : ws.send(m)));
    });

    const release = () =>
    {
        hold = false;

        for (const [ws, m] of held.splice(0))
            ws.send(m);
    };
    const relayStartsHeld = () => held.filter(([, m]) =>
    {
        const j = JSON.parse(m);

        return j.type === 'transport' && j.from === 'relay';
    }).length;
    const until = async (what, ms = 10000) =>
    {
        const end = Date.now() + ms;

        while (!await what() && Date.now() < end)
            await new Promise((r) => setTimeout(r, 10));
    };
    const lateNow = () => Promise.all(pages.map(({ page }) =>
        page.evaluate(() => window.jam.late().seen)));

    await enter(pages, 'jampassed', PAINT_PIECE);
    await A.page.evaluate(() => window.jam.play());
    await new Promise((r) => setTimeout(r, 2000));

    /* Ends the run, and holds each page's tape against genwav's for
       `piece' and its late count against `lateWas'. */
    const heard = async (piece, lateWas, what) =>
    {
        await new Promise((r) => setTimeout(r, 6000));
        await A.page.evaluate(() => window.jam.stop());
        await new Promise((r) => setTimeout(r, 3000));

        const results = [];

        for (const { label, page } of pages)
            results.push({ label, ...(await page.evaluate(() => ({
                tape: window.jam.tape(),
                sent: window.jam.sent(),
                late: window.jam.late().seen,
            }))) });

        const stop = results[0].sent.findLast((c) => c.op === 'stop' &&
                                                     c.at >= 0);
        const want = reference(piece, nodeBuild,
                               { commands: [stop], stopAt: stop.at });

        for (const [i, r] of results.entries())
        {
            const tape = tapeBefore(r.tape, stop.at);

            if (tape === want && r.late === lateWas[i])
                ok(`${what}: ${r.label} plays genwav's ${piece}, ` +
                   'nothing late');
            else
                fail(`${what}: ${r.label} counted ${r.late - lateWas[i]} ` +
                     'late' + (tape === want ? ''
                                             : `; ${firstDifference(want,
                                                                    tape)}`));
        }
    };

    let lateWas = await lateNow();

    hold = true;
    await switchTo(A, PIECE);
    await until(() => B.page.evaluate((p) => window.jam.piece() === p, PIECE));

    const edited = await A.page.evaluate((p) =>
    {
        const was = window.jam.file(p);
        const next = was.replace('prob = 0.6; hold = 5 s; vel = 50;',
                                 'prob = 1; hold = 5 s; vel = 90;');

        window.jam.setFile(p, next);

        return next;
    }, PIECE);

    await until(() => B.page.evaluate(
        ([p, t]) => window.jam.file(p) === t, [PIECE, edited]));
    release();
    await heard(PIECE, lateWas, 'a Play B\'s document had gone past');

    await A.page.evaluate(() => window.jam.play());
    await new Promise((r) => setTimeout(r, 2000));
    lateWas = await lateNow();
    hold = true;
    await switchTo(A, 'ebb.gen');
    await until(() => relayStartsHeld() === 1);
    await switchTo(A, SWITCH_PIECE);
    await until(() => relayStartsHeld() === 2);
    release();
    await heard(SWITCH_PIECE, lateWas, 'a Play replaced before B got to it');
}

/* A .gen pasted over the room's, naming other graphs: colony.gen's text
 * replaced by airports.gen's, which names amb01.dsp and not ts1.dsp, and
 * then by ebb.gen's, which names organ0.dsp, which the room lacks. The
 * tabs and the node editor's menu follow the text on both pages; ts1.dsp
 * stays in the document, which a paste does not take anything out of; and
 * the Apply brings organ0.dsp in from the shipped graphs, which both pages
 * then load. */
async function pasteTogether (pages)
{
    const [A] = pages;

    await enter(pages, 'jampaste', PAINT_PIECE);

    const [airports, ebbText] = await A.page.evaluate(() => Promise.all(
        ['airports.gen', 'ebb.gen'].map(
            (n) => fetch(`gen/${n}`).then((r) => r.text()))));

    await A.page.evaluate((t) => window.jam.setFile('colony.gen', t),
                          airports);

    for (const { label, page } of pages)
    {
        const wrong = await shows(page, [
            ['amb01.dsp', 'colony.gen', 'ts1.dsp'],
            ['amb01.dsp', 'colony.gen'],
            ['amb01.dsp']]);

        if (wrong === null)
            ok(`${label}'s tabs and node editor drop ts1.dsp when the ` +
               '.gen stops naming it, and the document keeps it');
        else
            fail(`${label} after a paste naming only amb01.dsp: ${wrong}`);
    }

    await A.page.evaluate((t) =>
    {
        window.jam.setFile('colony.gen', t);
        window.jam.apply();
    }, ebbText);

    for (const { label, page } of pages)
    {
        const wrong = await shows(page, [
            ['amb01.dsp', 'colony.gen', 'organ0.dsp', 'ts1.dsp'],
            ['amb01.dsp', 'colony.gen', 'organ0.dsp'],
            ['amb01.dsp', 'organ0.dsp']]);
        const loaded = await page.waitForFunction(
            () => window.jam.instruments().some((i) => i.dsp === 'organ0.dsp'),
            null, { timeout: 10000 }).then(() => true, () => false);

        if (wrong === null && loaded)
            ok(`${label} has organ0.dsp from the shipped graphs at the ` +
               'Apply, as a tab and in the node editor, and plays it');
        else
            fail(`${label} after a paste naming organ0.dsp: ${wrong}` +
                 (loaded ? '' : ', and it is not loaded'));
    }

    await A.page.evaluate(() => window.jam.stop());
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
        results.push({ label, ...(await page.evaluate(async () => ({
            tape: window.jam.tape(),
            sent: window.jam.sent(),
            late: window.jam.late(),
            played: await window.jam.rollPlayed(),
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

    /* Every seat's keys on each page's roll, at the time each was stamped
       for, which is when it sounded there -- not when it was pressed. */
    for (const r of results)
    {
        const played = r.played ?? [];
        const missing = keys.filter((c) => !played.some(
            ([at, channel, note, held]) => channel === c.seat &&
                note === c.note && !held && Math.abs(at - c.at) < 1e-3));

        if (missing.length === 0 && played.length === keys.length)
            ok(`${r.label}'s roll shows all ${keys.length} keys, both ` +
               'seats\', where they were stamped for');
        else
            fail(`${r.label}'s roll has ${played.length} played keys ` +
                 `of ${keys.length}; missing ` +
                 missing.map((c) => `${c.seat}:${c.note}@${c.at.toFixed(3)}`)
                     .join(', '));
    }
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

/* Keys seen from the other seat, in the room playTogether left stopped:
 * a direct key on the roll and lit on the keyboard while it is down, and
 * a bar-ahead key whose stamped off a Play throws away not left lit.
 */
async function keysSeen (pages)
{
    const [A, B] = pages;

    /* `test' of what A's page says, asked until it holds or time is up. */
    const until = async (test, ms = 8000) =>
    {
        for (const end = Date.now() + ms; Date.now() < end;)
        {
            const seen = await A.page.evaluate(async () => ({
                played: await window.jam.rollPlayed() ?? [],
                heard: window.jam.heard(),
            }));

            if (test(seen))
                return true;

            await new Promise((r) => setTimeout(r, 100));
        }

        return false;
    };

    const onRoll = (seen, note, held) => seen.played.some(
        ([, channel, n, h]) => channel === 0 && n === note && h === held);

    await B.page.evaluate(() =>
    {
        window.jam.mode('direct');
        window.jam.press(70, 90);
    });

    const down = await until((s) => onRoll(s, 70, true) &&
                                    s.heard.includes(70));

    await B.page.evaluate(() => window.jam.release(70));

    const up = await until((s) => onRoll(s, 70, false) &&
                                  !s.heard.includes(70));

    if (down && up)
        ok('a direct key from the other seat is on the roll and lit while ' +
           'it is down, and ended and out once it is let go');
    else
        fail(`a direct key from the other seat: down ${down}, up ${up}`);

    await A.page.evaluate(() => window.jam.play());
    await A.page.waitForFunction(() => window.jam.probe().running, null,
                                 { timeout: 10000 }).catch(() => {});
    await B.page.evaluate(() =>
    {
        window.jam.mode('ahead');
        window.jam.press(72, 90);
    });

    const lit = await until((s) => s.heard.includes(72));

    /* Its off goes out a bar on, and the Play is there first. */
    await B.page.evaluate(() => window.jam.release(72));
    await A.page.evaluate(() => window.jam.play());
    await new Promise((r) => setTimeout(r, 3000));

    const left = await A.page.evaluate(() => window.jam.heard());

    if (lit && !left.includes(72))
        ok('a key whose off a Play threw away is not left lit');
    else
        fail(`a key whose off a Play threw away: lit ${lit}, then ` +
             `${JSON.stringify(left)}`);

    await A.page.evaluate(() => window.jam.stop());
}

/* Chat between the two pages: a line typed into one is on the other under
 * its sender's name and the bar.beat it was sent at, the other's Play is a
 * line in the feed, and letters typed into the box are text -- on a page
 * with a seat, where the same letters are otherwise notes.
 */
async function chatTogether (pages)
{
    const [A, B] = pages;

    for (const { page } of pages)
    {
        await page.goto(`${url}&room=jamchat&name=${
            page === A.page ? A.label : B.label}&piece=${HANDS_PIECE}`);
        await page.waitForFunction(
            () => !document.getElementById('roompanel').hidden,
            null, { timeout: 15000 });
        await page.click('#start');
        await page.waitForFunction(() => window.jam.ready(), null,
                                   { timeout: 20000 });
    }

    const feed = (who) => who.page.evaluate(() =>
        [...document.querySelectorAll('#chatfeed li')]
            .map((li) => li.textContent).join(' | '));
    const shows = (who, cls, src) => who.page.waitForFunction(
        ([c, s]) => [...document.querySelectorAll(`#chatfeed .${c}`)]
            .some((li) => new RegExp(s).test(li.textContent)),
        [cls, src], { timeout: 5000 }).then(() => true, () => false);

    await B.page.evaluate(() => window.jam.seat(0));
    await B.page.waitForFunction(() => window.jam.seatNow() === 0, null,
                                 { timeout: 5000 });
    await A.page.evaluate(() => window.jam.play());

    /* Both are guests, and are shown as guests. */
    const guest = (label) => `${label} \\(guest\\)`;

    if (await shows(B, 'chatactivity', `^${guest(A.label)} pressed Play$`))
        ok(`${B.label}'s feed says ${A.label} pressed Play`);
    else
        fail(`${B.label}'s feed never said ${A.label} pressed Play: ` +
             await feed(B));

    await A.page.waitForFunction(() => window.jam.transportNow() > 0.5,
                                 null, { timeout: 10000 });
    await A.page.click('#chatinput');
    await A.page.keyboard.type('switch at 17');
    await A.page.keyboard.press('Enter');

    if (await shows(B, 'chatline',
                    `^\\d+\\.\\d+ ${guest(A.label)}: switch at 17$`))
        ok(`a line typed on ${A.label} is on ${B.label} with its name and ` +
           'bar.beat');
    else
        fail(`${A.label}'s line never reached ${B.label}: ${await feed(B)}`);

    const notes = () => B.page.evaluate(
        () => window.jam.sent().filter((c) => c.type === 'note').length);

    await B.page.evaluate(() => document.activeElement?.blur());

    const before = await notes();

    await B.page.keyboard.press('z');
    await new Promise((r) => setTimeout(r, 300));

    const held = await notes();

    await B.page.click('#chatinput');
    await B.page.keyboard.type('zsxdcvgbhnjm');
    await B.page.keyboard.press('Enter');

    const went = await shows(A, 'chatline',
                             `${guest(B.label)}: zsxdcvgbhnjm$`);
    const after = await notes();

    if (held > before && after === held && went)
        ok('letters typed into the chat box are a line and not notes, ' +
           'where the same letter outside it is a note');
    else
        fail(`a key outside the box sent ${held - before} notes, typing in ` +
             `it ${after - held}, and the line ${went ? 'went' : 'did not'}`);

    await A.page.evaluate(() => window.jam.stop());
}

/* An account and a guest in one room. The first page creates an account
 * in the dialog and saves its key, logs out, reloads, and logs in again
 * with the key typed as a person might; the second joins as a guest,
 * after being turned away under the account's handle. Both see the handle
 * as it is and the guest marked as one: in the peers, in chat, on a seat,
 * and on the cursor in the editor.
 */
async function accountsTogether (pages)
{
    const [A, B] = pages;
    const lobby = `${url}&room=jamaccounts&piece=${HANDS_PIECE}`;
    const dialog = (page) => page.locator('#accountdialog');

    await A.page.goto(lobby);
    await A.page.click('#account', { timeout: 10000 });
    await A.page.fill('#account-handle', 'Ann');
    await dialog(A.page).getByRole('button', { name: 'Create account' })
        .click();
    await A.page.waitForSelector('#account-save-key');

    const key = await A.page.inputValue('#account-save-key');

    await dialog(A.page).getByRole('button', { name: 'Save key' }).click();
    await dialog(A.page).getByRole('button', { name: 'Log out' }).click();
    await A.page.waitForSelector('#account-login-key');
    await A.page.click('#accountclose');

    if (key.split('-').length === 8 &&
        await A.page.evaluate(() => !document.getElementById('name').disabled))
        ok(`an account is made in the dialog, and its key is shown once`);
    else
        fail(`the dialog's key was "${key}"`);

    await A.page.reload();
    await A.page.click('#account', { timeout: 10000 });
    await A.page.fill('#account-login-key',
                      key.toUpperCase().replaceAll('-', ' '));
    await dialog(A.page).getByRole('button', { name: 'Log in' }).click();
    await A.page.waitForSelector('#account-newhandle');
    await A.page.click('#accountclose');

    const named = await A.page.evaluate(() =>
        [document.getElementById('name').value,
         document.getElementById('name').disabled]);

    if (named[0] === 'Ann' && named[1])
        ok('the key logs in on a reload, and the name is the handle');
    else
        fail(`logged in, the name box holds ${JSON.stringify(named)}`);

    /* Sent to another relay, the page is a guest there: no button, and
       the session neither sent nor dropped. */
    const other = await relay({ port: 0, host: '127.0.0.1', tree: top,
                                corsOrigin: '*' });

    try
    {
        await A.page.goto(`${url}&room=jamelsewhere&name=Mal&relay=` +
                          `ws://127.0.0.1:${other.address().port}`);
        await A.page.waitForFunction(
            () => !document.getElementById('roompanel').hidden, null,
            { timeout: 15000 });

        const there = [...other.rooms.get('jamelsewhere').peers.values()];
        const kept = await A.page.evaluate(() => Object.keys(localStorage)
            .filter((k) => k.startsWith('thinksynth:account:')).length);
        const button = await A.page.isVisible('#account');

        if (there.length === 1 && there[0].account === null &&
            there[0].name === 'Mal' && kept === 1 && !button)
            ok('another relay is joined as a guest, and not shown the ' +
               'session');
        else
            fail(`at another relay: ${JSON.stringify(there.map((p) =>
                [p.name, p.account]))}, ${kept} sessions kept, the ` +
                 `button ${button ? 'shown' : 'hidden'}`);
    }
    finally
    {
        other.shutdown();
    }

    /* A relay that is the site's but offers no accounts -- rolled back,
       or run without CORS_ORIGIN -- leaves a session kept for it alone:
       the name is the page's to change, and there is no button. */
    const bare = await relay({ port: 0, host: '127.0.0.1', tree: top });
    const bareUrl = `ws://127.0.0.1:${bare.address().port}`;

    try
    {
        await A.page.route('**/config.json',
                           (r) => r.fulfill({ json: { relay: bareUrl } }));
        await A.page.evaluate((origin) => localStorage.setItem(
            `thinksynth:account:${origin}`,
            JSON.stringify({ session: `s_${'3'.repeat(32)}`,
                             handle: 'Ann' })),
                              bareUrl.replace(/^ws/, 'http'));
        await A.page.goto(`${url}&room=jambare`);
        await new Promise((r) => setTimeout(r, 1500));

        const free = await A.page.evaluate(() =>
            [document.getElementById('name').disabled,
             document.getElementById('account').hidden]);

        if (!free[0] && free[1])
            ok('a home relay without accounts leaves the name free and ' +
               'shows no button');
        else
            fail(`a home relay without accounts: name ${free[0]
                ? 'locked' : 'free'}, button ${free[1] ? 'hidden' : 'shown'}`);

        await A.page.evaluate((origin) => localStorage.removeItem(
            `thinksynth:account:${origin}`), bareUrl.replace(/^ws/, 'http'));
        await A.page.unroute('**/config.json');
    }
    finally
    {
        bare.shutdown();
    }

    /* The site's config.json read once a load: were the join to read it
       again and get nothing, it would go to the default relay with the
       session kept for this one. */
    let configs = 0;

    await A.page.route('**/config.json',
                       (r) => (configs++ === 0 ? r.continue() : r.abort()));
    await A.page.goto(lobby);
    await A.page.waitForFunction(
        () => document.getElementById('name').value === 'Ann', null,
        { timeout: 10000 });

    await A.page.click('#join');

    if (await A.page.waitForFunction(
        () => !document.getElementById('roompanel').hidden, null,
        { timeout: 15000 }).then(() => true, () => false))
        ok(`the join goes where the page found its relay, config.json ` +
           `read ${configs} time${configs === 1 ? '' : 's'}`);
    else
        fail('the join went elsewhere: ' + await why(A.page));

    await A.page.unroute('**/config.json');

    await B.page.goto(`${lobby}&name=ann`);

    if (await B.page.waitForFunction(
        () => /account's handle/.test(document.getElementById('status')
                                         .textContent),
        null, { timeout: 10000 }).then(() => true, () => false))
        ok('a guest is turned away under the account\'s handle');
    else
        fail('a guest named ann joined: ' + await why(B.page));

    /* Opened before the network: the first config.json read fails, and
       the join reads it again rather than going to the default relay. */
    let unread = true;

    await B.page.route('**/config.json', (r) =>
    {
        if (unread)
        {
            unread = false;
            return r.abort();
        }

        return r.continue();
    });
    await B.page.goto(`${lobby}&name=Bo`);

    if (await B.page.waitForFunction(
        () => !document.getElementById('roompanel').hidden, null,
        { timeout: 15000 }).then(() => true, () => false))
        ok('a config.json that failed at the load is read again to join');
    else
        fail('a join after a failed config.json: ' + await why(B.page));

    await B.page.unroute('**/config.json');
    await B.page.evaluate(() => window.jam.seat(0));

    const peersOf = (page) => page.evaluate(() =>
        [...document.querySelectorAll('#peers .peer')]
            .map((p) => p.firstChild.textContent).sort().join(', '));
    const want = 'Ann, Bo (guest) (channel 1)';

    for (const who of [A, B])
    {
        const seen = await who.page.waitForFunction(
            (w) => [...document.querySelectorAll('#peers .peer')]
                .map((p) => p.firstChild.textContent).sort().join(', ') === w,
            want, { timeout: 10000 }).then(() => true, () => false);

        if (seen)
            ok(`${who.label}'s peers are ${want}`);
        else
            fail(`${who.label}'s peers are ${await peersOf(who.page)}`);
    }

    const said = (who, cls, src) => who.page.waitForFunction(
        ([c, s]) => [...document.querySelectorAll(`#chatfeed .${c}`)]
            .some((li) => new RegExp(s).test(li.textContent)),
        [cls, src], { timeout: 5000 }).then(() => true, () => false);

    for (const [from, to, line, src] of [
        [A, B, 'from an account', '^Ann: from an account$'],
        [B, A, 'from a guest', '^Bo \\(guest\\): from a guest$']])
    {
        await from.page.fill('#chatinput', line);
        await from.page.press('#chatinput', 'Enter');

        if (await said(to, 'chatline', src))
            ok(`${to.label}'s chat says ${src}`);
        else
            fail(`${to.label}'s chat never said ${src}`);
    }

    if (await said(A, 'chatactivity', '^Bo \\(guest\\) took channel 1$'))
        ok('and the guest\'s seat is marked as a guest\'s');
    else
        fail('the guest\'s seat is not in the feed as a guest\'s');

    /* A cursor each, with its name over it in the other's editor. */
    for (const { page } of [A, B])
        await page.click('#editor .cm-content');

    for (const [who, name] of [[A, 'Bo (guest)'], [B, 'Ann']])
    {
        const seen = await who.page.waitForFunction(
            (n) => [...document.querySelectorAll('.cm-ySelectionInfo')]
                .some((e) => e.textContent === n),
            name, { timeout: 10000 }).then(() => true, () => false);

        if (seen)
            ok(`${who.label}'s editor names the other cursor ${name}`);
        else
            fail(`${who.label}'s editor has no cursor named ${name}`);
    }

    /* The relay drops the account's room socket, as a restart or a lost
       network does. The page joins again by itself, and an edit made
       after reaches the other page. */
    for (const p of relayServer.rooms.get('jamaccounts').peers.values())
        if (p.name === 'Ann')
            p.ws.terminate();

    const back = await A.page.waitForFunction(
        () => /^Back in jamaccounts/.test(
            document.getElementById('status').textContent),
        null, { timeout: 15000 }).then(() => true, () => false);

    await A.page.evaluate(() => window.jam.setFile(
        'hands.gen', `# after the rejoin\n${window.jam.file('hands.gen')}`));

    const synced = await B.page.waitForFunction(
        () => window.jam.file('hands.gen')?.startsWith('# after the rejoin'),
        null, { timeout: 10000 }).then(() => true, () => false);

    if (back && synced)
        ok('a page whose room socket is cut joins again, and its edits ' +
           'reach the room');
    else
        fail(`after its room socket was cut: ${back ? 'rejoined' : 'not '
            + 'rejoined'}, the edit ${synced ? 'synced' : 'not synced'} -- ` +
             await why(A.page));

    await A.page.evaluate(() => localStorage.clear());
}

/* An account made with a passkey and logged back in with it, in a page of
 * its own on localhost -- a valid RP ID where 127.0.0.1 is not -- with
 * Chromium's virtual authenticator standing in for the person's. That one
 * answers whatever asks while its presence is simulated, the login form's
 * autofill offer included; the button is tried with autofill taken away.
 */
async function passkeysTogether (browser)
{
    const context = await browser.newContext();
    const page = await context.newPage();
    const dialog = page.locator('#accountdialog');
    const cdp = await context.newCDPSession(page);
    const listed = () => page.waitForFunction(
        () => [...document.querySelectorAll('#accountdialog .passkeys li')]
            .map((li) => li.firstChild.textContent).join('|'),
        null, { timeout: 10000 }).then((h) => h.jsonValue(), () => '');
    const loggedIn = () => page.waitForSelector('#account-newhandle',
                                                { timeout: 10000 })
        .then(() => true, () => false);
    const sessionOf = () => page.evaluate(() =>
    {
        const kept = Object.entries(localStorage).find(
            ([k]) => k.startsWith('thinksynth:account:'));

        return kept ? JSON.parse(kept[1]).session : null;
    });
    const said = async () => 'the dialog says "' +
        await page.textContent('#accountdialog [role=status]') +
        '", the log ends ' + JSON.stringify((await page.evaluate(
            () => document.getElementById('log').textContent))
            .split('\n').filter(Boolean).slice(-6));
    const logOut = async () =>
    {
        await dialog.getByRole('button', { name: 'Log out' }).click();
        await page.waitForSelector('#account-login-key');
    };

    page.on('pageerror', (e) => errors.push(`passkeys: ${e.message}`));

    try
    {
        await cdp.send('WebAuthn.enable');

        const { authenticatorId } = await cdp.send(
            'WebAuthn.addVirtualAuthenticator', { options: {
                protocol: 'ctap2', transport: 'internal', hasResidentKey: true,
                hasUserVerification: true, isUserVerified: true,
                automaticPresenceSimulation: true } });
        const present = (enabled) => cdp.send(
            'WebAuthn.setAutomaticPresenceSimulation',
            { authenticatorId, enabled });

        await page.goto(`http://localhost:${sitePort}/jam.html?panes=0` +
                        `&room=jampasskeys&piece=${HANDS_PIECE}`);
        await page.click('#account', { timeout: 10000 });
        await page.fill('#account-handle', 'Pia');
        await dialog.getByRole('button', { name: 'Create account' }).click();
        await page.waitForSelector('#account-save-key');

        const key = await page.inputValue('#account-save-key');
        const recovery = await dialog.getByRole('heading',
                                                { name: 'Your recovery key' })
            .isVisible();

        await dialog.getByRole('button', { name: 'Save key' }).click();

        const made = await listed();

        if (key.split('-').length === 8 && recovery &&
            /^Passkey.*, added .*, not used yet $/.test(made))
            ok('an account is made with a passkey, and the key shown as ' +
               'its recovery key');
        else
            fail(`made with a passkey: key "${key}", recovery heading ` +
                 `${recovery}, passkeys "${made}"`);

        /* The authenticator answers the offer as soon as it is made, so
           the logged-out screen may be gone before anything here sees it:
           what says it happened is a new session. */
        const before = await sessionOf();

        await dialog.getByRole('button', { name: 'Log out' }).click();

        const after = await page.waitForFunction((was) =>
        {
            const kept = Object.entries(localStorage).find(
                ([k]) => k.startsWith('thinksynth:account:'));
            const now = kept && JSON.parse(kept[1]).session;

            return now && now !== was &&
                   document.getElementById('account-newhandle') !== null
                ? now : null;
        }, before, { timeout: 15000 }).then((h) => h.jsonValue(),
                                            () => null);

        if (after !== null)
            ok('the passkey the login form\'s autofill offers logs in');
        else
            fail(`the autofill offer never logged in: ${await said()}`);

        /* Away while the offer is still out, and back once the page has
           none to make. */
        await page.addInitScript(() =>
        {
            delete PublicKeyCredential.isConditionalMediationAvailable;
        });
        await present(false);
        await logOut();
        await page.reload();
        await present(true);
        await page.click('#account', { timeout: 10000 });
        await dialog.getByRole('button', { name: 'Log in with a passkey' })
            .click();

        const back = await loggedIn();
        const used = await listed();
        const name = await page.inputValue('#name');

        if (back && /, last used /.test(used) && !used.includes('|') &&
            name === 'Pia')
            ok('so does the button, on a reload, and the passkey is listed ' +
               'as used');
        else
            fail(`logged in with the passkey: the name box holds ` +
                 `"${name}", passkeys "${used}"; ${await said()}`);

        /* A new key takes the passkeys, and fills itself in to add one
           with. */
        await page.fill('#account-replace-key', key);
        await dialog.getByRole('button', { name: 'Replace key' }).click();
        await page.waitForSelector('#account-save-key');

        const newKey = await page.inputValue('#account-save-key');

        await dialog.getByRole('button', { name: 'Save key' }).click();

        const emptied = await listed();
        const filled = await page.inputValue('#account-passkey-key');

        await dialog.getByRole('button', { name: 'Add a passkey' }).click();

        const readded = await page.waitForFunction(
            () => /not used yet/.test(document.querySelector(
                '#accountdialog .passkeys')?.textContent),
            null, { timeout: 10000 }).then(() => true, () => false);

        if (emptied === 'None yet.' && filled === newKey && readded)
            ok('a new key removes the passkeys, and adds one with itself');
        else
            fail(`after a new key: passkeys "${emptied}", the add form ` +
                 `${filled === newKey ? 'filled' : 'not filled'}, ` +
                 `${readded ? '' : 'none '}added again`);
    }
    catch (e)
    {
        fail(`passkeys threw: ${e.message.split('\n')[0]}; ` +
             await said().catch(() => 'the page is gone'));
    }
    finally
    {
        await context.close();
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

/* The site first, telling the relay's URL once there is one: the relay
   takes a passkey's answer only from the page's origin. */
let relayUrl = null;
const site = await serve(build, 0, '127.0.0.1', () => relayUrl);
const sitePort = site.address().port;
const relayServer = await relay({
    port: 0, host: '127.0.0.1', tree: top, corsOrigin: '*',
    passkeys: { rpId: 'localhost', rpName: 'jamtest',
                origin: `http://localhost:${sitePort}` } });

relayUrl = `ws://127.0.0.1:${relayServer.address().port}`;
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

        if (await page.isVisible('#start'))
            fail(`${label} still offers Start once started`);

        /* The invite is the room's address with nobody's name in it:
           followed, it is a join as whoever follows it. */
        await page.evaluate(() =>
        {
            navigator.clipboard.writeText = async (text) =>
            {
                window.copied = text;
            };
        });
        await page.click('#invite');

        const copied = await page.evaluate(() => window.copied ?? '');
        const invite = URL.canParse(copied) ? new URL(copied) : null;

        if (invite?.searchParams.get('room') === 'jamtest' &&
            !invite.searchParams.has('name'))
            ok(`${label}'s invite link is ${invite.search}`);
        else
            fail(`${label}'s invite link is "${copied}"`);
    }

    /* Seen each other, by whatever path -- given the ten seconds the
       mesh gives a channel to open before it falls back. */
    for (const { label, page } of pages)
    {
        await page.waitForFunction(
            () => window.jam.peers().every((p) => p.path !== 'connecting'),
            null, { timeout: 15000 }).catch(() => {});

        const peers = await page.evaluate(() => window.jam.peers());
        const other = peers.find((p) => p.name !== `${label} (guest)`);

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

        /* Before joining, the room is in the list, with the two in it. */
        await page.goto(url);

        const listed = await page.waitForFunction(() =>
            [...document.querySelectorAll('#rooms li')]
                .find((li) => /^jamtest 2 people\b/.test(li.textContent))
                ?.textContent,
            null, { timeout: 15000 }).then((h) => h.jsonValue(), () => null);

        if (listed !== null && /, playing$/.test(listed))
            ok(`the room list shows it: ${listed}`);
        else
            fail(`the room list does not show jamtest with two people ` +
                 `playing: ${await page.evaluate(() =>
                     document.getElementById('rooms').textContent)}`);

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

            /* JOIN_AT seconds of the run to step through at a budget's
               worth a window: more than one quantum's. */
            const quanta = await page.evaluate(
                () => window.jam.catchQuanta());

            if (quanta > 0)
                ok(`and its catch-up spanned ${quanta + 1} quanta`);
            else
                fail('its catch-up ran inside one process() call');
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

    /* ---- and sees the other seat's keys ---- */

    await keysSeen(pages);

    /* ---- and plays recordings ---- */

    await sampleTogether(pages);

    /* ---- and clicks a cell in a Sequencer ---- */

    await sequenceTogether(pages);

    /* ---- and picks an instrument for a seat while it plays ---- */

    await pickTogether(pages);
    await pickRefused(pages);

    /* ---- and switches the piece, or pastes another ---- */

    await switchRaces(pages);
    await switchRacePlaying(pages);
    await passedTogether(pages);
    await switchTogether(pages, browsers[0]);
    await pasteTogether(pages);

    /* ---- and talks ---- */

    await chatTogether(pages);

    /* ---- as an account, and a guest ---- */

    await accountsTogether(pages);
    await passkeysTogether(browsers[0]);

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
