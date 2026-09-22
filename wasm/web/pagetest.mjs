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
 * pagetest.mjs -- the solo page's hands, dials and pictures, in a browser.
 *
 *   cd wasm/web && npm ci && npx playwright install chromium
 *   node pagetest.mjs [BUILD_DIR]
 *
 * The other harnesses here go round the page: browsertest.mjs builds the
 * synth the way host.js does and compares samples, piececheck.mjs never
 * opens a browser at all. Nothing ran main.js. So the two things the solo
 * page and the room page share -- the computer keyboard as a musical one,
 * and the knobs a piece declared as sliders (keyboard.js, panel.js) --
 * were only ever exercised by hand, on the page a change to either is
 * most likely to break.
 *
 * The parameter panel is here for the half of it that needs a browser: that
 * the module's description of a channel's controls became elements, and
 * that moving one reaches the arg. What the description says, and that it
 * is the same description the desktop draws, is scripts/panelcheck's and
 * wasm/web/panelcheck.mjs's.
 *
 * Small on purpose: the octave, the sliders, the channel's parameter panel,
 * a key down and up, and a key typed into a text box, which must play
 * nothing. Then the two canvases this page has: the composer view, and the
 * instrument's graph. What sounds is browsertest.mjs's business and
 * jamtest.mjs's; this is about the page.
 *
 * And then the composer view, which is the one thing here with a whole
 * second engine behind it: the piece's picture is drawn by the mirror --
 * another instance of the module, in a worker, fed the messages the
 * worklet is fed -- and comes over as a list of ops the page replays on a
 * Canvas2D. What is checked is the round trip a finger makes: the picture
 * arrives and is replayed, a stage whose picture is a control can be
 * enlarged, a drag on it leaves as a command and comes back as a board
 * that has changed, and Escape puts it back. The transport is left stopped
 * for that, so that nothing but the drag could have changed what is drawn.
 *
 * Exit status is the number of failures.
 */

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { chromium } from 'playwright';

import { serve } from './serve.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const top = path.join(here, '..', '..');
const build = path.resolve(process.argv[2] ?? path.join(top, 'build-web'));

const PIECE = 'airports.gen';

/* One with a picture that is a control: gen::life, which is what the
   composer view's gestures are tried on. */
const COMPOSER_PIECE = 'colony.gen';

/* A piece that names channels and declares no instrument for any of them,
   so the page has to aim them and offers a menu per channel. */
const AIMED_PIECE = 'fern.gen';

let failures = 0;

function check (cond, what)
{
    if (cond)
        process.stdout.write(`ok    ${what}\n`);
    else
    {
        failures++;
        process.stdout.write(`FAIL  ${what}\n`);
    }
}

if (!fs.existsSync(path.join(build, 'main.js')))
{
    process.stdout.write(`pagetest: no site in ${build}; build it first ` +
                         '-- see wasm/web/CMakeLists.txt.\n');
    process.exit(1);
}

const site = await serve(build, 0, '127.0.0.1', null);

/* The document rather than the tiled layout. Both are the page -- panes.js
   adopts what is in the markup and puts it back, and below 60em or under
   a finger the tiled one is not offered at all -- and this harness is
   about what the page plays rather than where it puts it. panecheck.mjs
   is the other one, and takes the same page across that threshold and
   back. */
const url = `http://127.0.0.1:${site.address().port}/index.html?panes=0`;
const errors = [];

const browser = await chromium.launch(
    { args: ['--autoplay-policy=no-user-gesture-required'] });

try
{
    const page = await browser.newPage();

    page.on('pageerror', (e) => errors.push(e.message));
    page.on('console', (m) =>
    {
        if (m.type() === 'error')
            errors.push(m.text());
    });

    await page.goto(url);
    await page.waitForFunction(
        () => document.getElementById('range').textContent !== '');

    /* The octave, before anything has started: the keys are drawn and the
       buttons move them whether or not there is a synth. */
    const range = await page.textContent('#range');

    check(/^C3/.test(range), `the range shows at load: ${range}`);

    await page.click('#up');

    const up = await page.textContent('#range');

    check(/^C4/.test(up), `an octave up: ${up}`);

    await page.click('#down');
    check(await page.textContent('#range') === range, 'and an octave back');

    /* Start, then a piece, and the knobs it declared. */
    await page.click('#start');
    await page.waitForFunction(
        () => !document.getElementById('loadpiece').disabled,
        null, { timeout: 60000 });
    check(true, 'the synth started');

    /* ---- the instrument's parameters ---- */

    /* The panel this page has never had. Patch mode, because that is the
       simplest thing it can be over: one .dsp on one channel, loaded by
       Start.

       What is checked here is the half that needs a browser -- that the
       module's description became elements, and that moving one of them
       reaches the arg. That the description itself is right, and is the
       same description the desktop draws, is panelcheck's and
       panelcheck.mjs's. */
    await page.waitForSelector('#params .panelrow', { timeout: 60000 });

    const chanPanel = await page.evaluate(() => window.solo.chanParams());
    const drawn = await page.evaluate(() => ({
        rows: document.querySelectorAll('#params .panelrow').length,
        groups: document.querySelectorAll('#params .panelgroup').length,
        sliders: document.querySelectorAll(
            '#params input[type="range"]').length,
    }));

    check(chanPanel !== null && drawn.rows === chanPanel.rows.length,
          `the channel's parameters drew: ${drawn.rows} rows of ` +
          `${chanPanel?.rows?.length}`);

    check(drawn.groups === chanPanel.groups.length,
          `and a foldable block per group: ${drawn.groups} of ` +
          `${chanPanel.groups.length} (${chanPanel.groups.join(', ')})`);

    /* Every row of the .dsp this page loads is a slider; a selector would
       be a <select> and is counted out here so the claim stays exact. */
    const ranges = chanPanel.rows.filter((r) => r.kind === 0).length;

    check(drawn.sliders === ranges,
          `and a slider for each of the ${ranges} that is one`);

    /* A nudge, with the keyboard, because that is a real input event from
       the browser rather than a synthesised one.
     *
       Then the poll, which pushes what the module holds back into the
       widgets. That is the assertion: if the edit had not reached the arg
       the slider would snap back to where it was, since the page draws
       what the module says and never what it typed. */
    await page.evaluate(() =>
        document.querySelectorAll('#params input[type="range"]')[0].focus());

    const wasParam = await page.evaluate(() =>
        document.querySelectorAll('#params input[type="range"]')[0].value);

    await page.keyboard.press('ArrowRight');
    await page.keyboard.press('ArrowRight');

    const nudged = await page.evaluate(() =>
        document.querySelectorAll('#params input[type="range"]')[0].value);

    await page.evaluate(() => window.solo.pollChanParams());

    const polled = await page.evaluate(() =>
    {
        const range = document.querySelectorAll(
            '#params input[type="range"]')[0];

        return { value: range.value,
                 shown: range.nextElementSibling.value };
    });

    check(nudged !== wasParam && polled.value === nudged,
          `moving one reaches the arg and survives the poll: ` +
          `${wasParam} -> ${nudged}, and the module says ${polled.value}`);

    check(Number(polled.shown) === Number(nudged),
          `and the number box beside it agrees: ${polled.shown}`);

    /* And the poll does not type over the person.
     *
       The module's value arrives four times a second whether or not anyone
       asked, and it used to be written into every box on the panel. A number
       takes longer than a quarter second to type, so the digits were being
       replaced by the value that was still there -- an edit that could not be
       made at all rather than one that failed. Half a number is left in the
       box here, deliberately unconfirmed. */
    await page.evaluate(() =>
        document.querySelector('#params .value').focus());

    await page.keyboard.press('Control+A');
    await page.keyboard.type('0.12');

    await page.evaluate(() => window.solo.pollChanParams());
    await page.evaluate(() => window.solo.pollChanParams());

    const typing = await page.evaluate(() =>
        document.querySelector('#params .value').value);

    check(typing === '0.12',
          `a half-typed number survives the poll: "${typing}"`);

    /* While it is being typed into, the box is the only thing on the panel
       that is not showing the module: the slider beside it is the same row
       and keeps following. */
    const apart = await page.evaluate(() =>
    {
        const box = document.querySelector('#params .value');

        return { shown: box.value, range: box.previousElementSibling.value };
    });

    check(Number(apart.shown) !== Number(apart.range),
          `and only that box holds back: box ${apart.shown}, slider ` +
          `${apart.range}`);

    /* And it is let go of the moment it stops being typed into. Leaving the
       box is what confirms the number, so this is the edit landing and the
       box going back to showing what the module has -- which is the same
       thing, and is why the two agree again. */
    await page.evaluate(() => document.querySelector('#params .value').blur());
    await page.evaluate(() => window.solo.pollChanParams());

    const together = await page.evaluate(() =>
    {
        const box = document.querySelector('#params .value');

        return { shown: box.value, range: box.previousElementSibling.value };
    });

    check(Number(together.shown) === Number(together.range),
          `and the box follows the arg again once it is left alone: box ` +
          `${together.shown}, slider ${together.range}`);

    await page.selectOption('#mode', 'piece');
    await page.selectOption('#piece', PIECE);

    /* There are three loads on the way here, and every one of them ends
       by replacing every element in the knobs row: the mode switch loads
       what piece mode is about to play, choosing a piece loads that, and
       Load loads it again -- deliberately, since Load re-reads the .gen
       in the textarea, which is editable.

       This used to wait for "a slider that is not the one the first load
       drew", on the reasoning that there were two. There are three, and
       the handle the wait compares against can already be stale when the
       wait first runs, in which case it returns at once having waited for
       nothing. Then the third load lands in the middle of the nudge
       below, the focused slider is replaced under it, the arrow key goes
       to <body>, and the value has not moved -- which is what CI printed,
       on a machine slow enough to let the load get that far behind.

       So the page is asked instead. It knows what it has queued; nothing
       out here can know it by counting. */
    await page.waitForSelector('#knobs input[type="range"]',
                               { timeout: 60000 });
    await page.click('#loadpiece');
    await page.evaluate(() => window.solo.settled());

    /* Drawn by panel.js off the module's own description of them
       (src/KnobPanel.cpp), so a row carries the number a command names it
       by as its id and the value spelled at the resolution its range asks
       for -- not at whatever three significant figures came to. */
    const knobs = await page.evaluate(() =>
        [...document.querySelectorAll('#knobs .panelrow')].map((line) =>
        {
            const range = line.querySelector('input[type="range"]');

            return { label: line.querySelector('label').textContent,
                     value: range.value,
                     shown: range.nextElementSibling.value };
        }));

    check(knobs.length > 0 &&
          knobs.every((k) => k.shown !== '' && k.label !== ''),
          `${PIECE}'s knobs drew, each with its label and its value: ` +
          knobs.map((k) => `${k.label}=${k.shown}`).join(', '));

    /* Moving a slider moves the number beside it -- the box a remote
       peer's move writes to on the room page as well.
     *
       With the keyboard, because that is a real input event from the
       browser: fill() sets the value itself and synthesises one, which
       is a weaker claim about a range input and a poor one to debug.
       Both numbers go in the message, since a slider that did not move
       and a number that did not follow it are different bugs. */
    const firstKnob = '#knobs .panelrow input[type="range"]';

    await page.focus(firstKnob);

    const was = await page.inputValue(firstKnob);

    await page.keyboard.press('ArrowRight');

    let now = await page.inputValue(firstKnob);

    /* At the top of its range there is nowhere rightwards to go. */
    if (now === was)
    {
        await page.keyboard.press('ArrowLeft');
        now = await page.inputValue(firstKnob);
    }

    const shown = await page.evaluate(
        (sel) => document.querySelector(sel).nextElementSibling.value,
        firstKnob);

    check(now !== was && Number(shown) === Number(now),
          `a nudge moves the slider and the number beside it: ` +
          `${was} -> ${now}, showing ${shown}`);

    /* An emptied box is not a knob set to zero.
     *
       A knob is delivered by a stamped command and not by tw_panel_edit, so
       nothing between the box and the write reads what is in it: Number('')
       is 0, and 0 is outside the range of plenty of a piece's knobs. Nor is
       a number typed past the end of the range applied unheld while the
       slider beside it clamps. */
    const knobBox = '#knobs .panelrow .value';

    await page.fill(knobBox, '');
    await page.evaluate((sel) => document.querySelector(sel).blur(), knobBox);
    await new Promise((r) => setTimeout(r, 300));

    const emptied = await page.evaluate((sel) =>
    {
        const box = document.querySelector(sel);

        return { shown: box.value,
                 range: box.previousElementSibling.value };
    }, knobBox);

    check(Number(emptied.range) === Number(now) &&
          Number(emptied.shown) === Number(now),
          `emptying a knob's box moves nothing: still ${emptied.shown}`);

    /* And past the end of its travel it goes to the end and not past it. */
    const knobMax = await page.evaluate((sel) =>
        document.querySelector(sel).previousElementSibling.max, knobBox);

    await page.fill(knobBox, String(Number(knobMax) * 10 + 1));
    await page.evaluate((sel) => document.querySelector(sel).blur(), knobBox);
    await new Promise((r) => setTimeout(r, 300));

    const past = await page.evaluate((sel) =>
    {
        const box = document.querySelector(sel);

        return { shown: box.value,
                 range: box.previousElementSibling.value };
    }, knobBox);

    check(Number(past.shown) === Number(knobMax) &&
          Number(past.range) === Number(knobMax),
          `and a number past its end is held to it: ${past.shown} of ` +
          `${knobMax}`);

    /* A computer key holds an on-screen key and lets it go.
     *
       The Escape is how the page hands the keyboard back from a box that
       is being typed into, and it is all this needs now. It used to
       reach in and blur whatever had the focus, because a slider dragged
       or a list chosen from kept the letters for itself; neither does
       any more (keyfocus.js). */
    await page.keyboard.press('Escape');
    await page.keyboard.down('z');
    await new Promise((r) => setTimeout(r, 200));

    const held = await page.evaluate(
        () => document.querySelectorAll('#keys .held').length);

    await page.keyboard.up('z');
    await new Promise((r) => setTimeout(r, 200));

    const released = await page.evaluate(
        () => document.querySelectorAll('#keys .held').length);

    check(held === 1 && released === 0,
          'a computer key holds one key on screen and lets it go ' +
          `(${held} down, ${released} up)`);

    /* And the same key typed into a text box plays nothing. */
    await page.evaluate(() =>
    {
        const d = document.getElementById('piecesource');

        d.hidden = false;
        d.open = true;
    });
    await page.click('#gen');
    await page.keyboard.down('z');
    await new Promise((r) => setTimeout(r, 200));

    const typed = await page.evaluate(
        () => document.querySelectorAll('#keys .held').length);

    await page.keyboard.up('z');
    check(typed === 0,
          'and a key typed into the source is editing, not a note');

    /* And a list touched with the pointer does not take them.
     *
       Choosing a piece or a patch left the focus on the <select>, and
       from then on every letter went to the list -- a keyboard that had
       to be won back by finding somewhere harmless to click. The Escape
       is for the browser's own popup, which the click opens and which
       owns the keyboard while it is up; the list keeps the focus after
       it, which is the state that used to be silent. What the rule is,
       and what happens to a list that is tabbed to rather than clicked,
       is keycheck.mjs's. */
    await page.keyboard.press('Escape');
    await page.click('#piece');
    await page.keyboard.press('Escape');

    await page.keyboard.down('z');
    await new Promise((r) => setTimeout(r, 200));

    const afterList = await page.evaluate(() => ({
        held: document.querySelectorAll('#keys .held').length,
        focused: document.activeElement?.id ?? '',
    }));

    await page.keyboard.up('z');

    check(afterList.held === 1,
          'and a key after the piece list is a note, with the list still ' +
          `focused (${afterList.focused || 'nothing'})`);

    /* ---- the composer view ---- */

    /* A piece with a picture that is a control: colony's Life board.
       Choosing it loads it; the mirror is sent the same load and says
       what it has, which is where the buttons below come from. */
    await page.selectOption('#piece', COMPOSER_PIECE);
    await page.waitForSelector('#composerstages button', { timeout: 60000 });

    /* The first frame that reaches the page: the canvas is sized to the
       drawing and the drawing is on it. A blank canvas of the right size
       would mean the list arrived and replayed into nothing. */
    await page.waitForFunction(() =>
    {
        const c = document.getElementById('composer');

        return c.width > 0 && c.height > 0;
    }, null, { timeout: 60000 });

    const ink = () => page.evaluate(() =>
    {
        const c = document.getElementById('composer');
        const d = c.getContext('2d').getImageData(0, 0, c.width, c.height)
                   .data;
        let sum = 0;

        for (let i = 0; i < d.length; i += 4)
            sum += d[i] + d[i + 1] + d[i + 2];

        return sum;
    });

    const size = await page.$eval('#composer',
                                  (c) => ({ w: c.width, h: c.height }));

    check(await ink() > 0,
          `the piece's picture drew, ${size.w} by ${size.h} device pixels`);

    /* And it is drawn to the width of the view, not squeezed into its
       height. A composer canvas is one row per chain, so fitting both
       dimensions lets a tall piece decide the zoom -- ten chains in a box
       half a screen tall came out at a quarter scale, which is what this
       is here to stop (CanvasContent::zoomToWidth). */
    const fitted = await page.evaluate(() =>
    {
        const c = document.getElementById('composer');
        const s = document.getElementById('composerscroll');

        return { wide: s.scrollWidth > s.clientWidth + 1,
                 width: parseFloat(c.style.width),
                 box: s.clientWidth };
    });

    check(!fitted.wide && fitted.width > fitted.box * 0.5,
          `and to the width of the view: ${fitted.width} in ${fitted.box}, ` +
          `${fitted.wide ? 'scrolling sideways' : 'no sideways scroll'}`);

    const stages = await page.$$eval('#composerstages button',
                                     (bs) => bs.map((b) => b.textContent));

    check(stages.length > 0,
          `${COMPOSER_PIECE} offers its controls: ${stages.join(', ')}`);

    await page.click('#composerstages button');
    await page.waitForFunction(
        () => /^Painting /.test(
            document.getElementById('composerstatus').textContent),
        null, { timeout: 60000 });
    check(true, `${stages[0]} enlarged`);

    /* A drag across the enlarged board. Nothing is playing, so the board
       changes only if the drag reached the composer -- which it can only
       do by leaving the canvas as a gesture, being stamped as a command,
       and being applied in the mirror at its time. */
    const before = await ink();

    /* The scroller's box and not the canvas's: the element is as big as
       the whole drawing and the scroller clips it, so a point outside
       what is on screen is a point some other element receives. The
       enlarged picture fills the visible part by construction -- it
       follows the viewport, which is the reason the content is told what
       the viewport is at all -- so the middle of this is the middle of
       the board. */
    await page.locator('#composerscroll').scrollIntoViewIfNeeded();

    const box = await page.$eval('#composerscroll', (d) =>
    {
        const r = d.getBoundingClientRect();

        return { x: r.x, y: r.y, w: d.clientWidth, h: d.clientHeight };
    });

    await page.mouse.move(box.x + box.w * 0.3, box.y + box.h * 0.5);
    await page.mouse.down();

    for (let i = 1; i <= 10; i++)
    {
        await page.mouse.move(box.x + box.w * (0.3 + 0.04 * i),
                              box.y + box.h * 0.5);
        await new Promise((r) => setTimeout(r, 40));
    }

    await page.mouse.up();
    await new Promise((r) => setTimeout(r, 1000));

    const after = await ink();

    check(before !== after,
          'a drag on the enlarged board went out as commands and came ' +
          'back as a board that has changed');

    await page.keyboard.press('Escape');
    await page.waitForFunction(
        () => !/^Painting /.test(
            document.getElementById('composerstatus').textContent),
        null, { timeout: 60000 });
    check(true, 'and Escape puts it back');

    /* A stage's params handle -- the three little sliders in its title
       bar -- asks for a popover beside the box, and what goes in it comes
       from the piece the mirror is holding: the values as it is playing
       them, the units the plugin declared, and the piece knob a param is
       read through where there is one. The canvas is asked where the
       handle is, since the layout is its own. */
    const handle = await page.evaluate(() => window.solo.handleOf(0, 0));

    if (handle.x < 0)
        check(false, 'the first stage has no params handle');
    else
    {
        await page.mouse.click(box.x + handle.x, box.y + handle.y);
        await page.waitForFunction(
            () => !document.getElementById('composerparams').hidden,
            null, { timeout: 15000 });

        const title = await page.textContent('#composerparams .menutitle');
        const rows = await page.evaluate(() => window.solo.params());

        check(rows.length > 0,
              `the params handle opens ${title}: ${rows.join(', ')}`);

        /* And it goes away with the next press somewhere else. */
        await page.mouse.click(box.x + box.w / 2, box.y + box.h - 4);
        check(await page.$eval('#composerparams', (e) => e.hidden),
              'and the next press closes it');
    }

    /* ---- a channel the piece left for the page to aim ---- */

    /* A piece that names channels and declares no instrument of its own is
     * the case patch.js exists for: what sounds there is the page's default,
     * and a person may choose something else. Both halves go through the
     * module now -- it reads the .patch and puts it on the channel -- so what
     * is checked here is that a choice arrives, which is the one part of that
     * path no headless gate walks.
     */
    await page.selectOption('#mode', 'piece');
    await page.selectOption('#piece', AIMED_PIECE);
    await page.waitForSelector('#channels select', { timeout: 60000 });
    await page.evaluate(() => window.solo.settled());

    const rows = await page.$$eval('#channels .channel', (all) => all.length);

    check(rows > 0, `${AIMED_PIECE} left ${rows} channels for the page to aim`);

    /* A channel this piece actually aimed, with the parameters pane moved
     * onto it so that the patch chosen below and the controls moved after
     * it are the same channel.
     *
     * Not simply the pane's own channel: the keys' channel is in the list
     * whether or not the piece named it, and a channel with nothing on it
     * has no graph, no patches over that graph, and nothing for the rest
     * of this to be about. */
    const chan = await page.evaluate(() =>
    {
        const row = [...document.querySelectorAll('#channels .channel')]
            .find((r) => r.querySelector('select')?.value !== '');

        return Number(row?.querySelector('.edited')?.dataset.channel ?? -1);
    });

    check(chan >= 0, `${AIMED_PIECE} left an instrument on a channel`);

    await page.selectOption('#paramchan', String(chan));
    await page.waitForFunction(
        (c) => document.getElementById('paramwhat').textContent
                       .endsWith(`channel ${c + 1}`),
        chan, { timeout: 60000 });

    /* The first .patch the panel offers over this channel's graph, chosen.
     *
     * The panel and not the channels row: a patch is a .dsp and a column
     * of values over its knobs, so the menus ask the two questions at the
     * two altitudes -- which graph, in the row that aims the channel, and
     * which of the patches somebody saved over it, here among the
     * controls those values set.
     *
     * The status line says what went on by the title the file gives
     * itself, which only something that has read the file knows and
     * nothing on the page reads one. */
    const chosen = await page.evaluate(() =>
        [...document.getElementById('parampatch').options]
            .find((o) => o.value.endsWith('.patch'))?.value ?? '');

    check(chosen !== '', `channel ${chan + 1} offers a .patch to choose`);

    if (chosen !== '')
    {
        await page.selectOption('#parampatch', chosen);
        await page.waitForFunction(
            () => /on channel \d+\. Play\.$/.test(
                document.getElementById('status').textContent),
            null, { timeout: 60000 });
        await page.evaluate(() => window.solo.settled());

        check(true, `choosing ${chosen} loads it: ` +
                    `${await page.textContent('#status')}`);

        /* And the row says whether what is on the channel is still what the
         * file says. A patch just read is not edited; moving one of its
         * controls makes it so. The desktop has lit a Save button off this
         * since 2004, and the page could not say it at all until the slots
         * were shared (src/PatchSet.h).
         */
        const edited = () => page.$eval(
            `#channels .edited[data-channel="${chan}"]`, (m) => !m.hidden);

        check(await edited() === false,
              'and the row does not say it has been edited');

        /* Through the panel, which is how a person would do it: the page
           has no other door to a chanarg. An arrow key on the slider, for
           the reason the nudge above uses one -- it is one edit, and it is
           the one a person makes. */
        await page.evaluate(() => window.solo.pollChanParams());
        await page.focus('#params input[type="range"]');
        await page.keyboard.press('ArrowRight');

        await page.waitForFunction(
            (c) => !document.querySelector(
                `#channels .edited[data-channel="${c}"]`).hidden,
            chan, { timeout: 60000 });

        check(true, 'and moving one of its controls says it has');

        /* And Save hands the file over. A page has nowhere to write to, so
         * what a Save is here is a download -- which is what the desktop's
         * is too, from where a person is standing. The bytes are the
         * application's (thPatchCapture, thPatchCompose), so what comes
         * down opens there.
         */
        const [download] = await Promise.all([
            page.waitForEvent('download', { timeout: 60000 }),
            page.click(`#channels .save[data-channel="${chan}"]`),
        ]);

        const saved = fs.readFileSync(await download.path(), 'utf8');

        check(saved.startsWith('# Thinksynth Patch File\n') &&
              /^dsp \S+$/m.test(saved),
              `Save downloads ${download.suggestedFilename()}, ` +
              `${saved.split('\n').length} lines of .patch`);

        /* And what came down is what is on the channel, edit and all: the
           value moved above is in the file, not the one it was loaded
           with. */
        const wrote = await page.evaluate(
            (c) => window.solo.chanParams()?.rows
                .find((r) => r.editable && r.kind === 0)?.id, chan);

        check(wrote === undefined ||
              new RegExp(`^${wrote} `, 'm').test(saved),
              `and it carries ${wrote}, the control that was moved`);

        /* Saved is not edited. Neither shell watches the file afterwards,
           so this is as far as either of them can tell. */
        await page.waitForFunction(
            (c) => document.querySelector(
                `#channels .edited[data-channel="${c}"]`).hidden,
            chan, { timeout: 60000 });

        check(true, 'and the row stops saying it has been edited');

        /* And a patch chosen by hand takes the row with it. The mark says
           what is on the channel now, and what is on it after a choice is a
           file just read -- so the mark goes out whatever the patch before
           it had become. */
        const another = await page.evaluate(({ c, was }) =>
        {
            const row = [...document.querySelectorAll('#channels .channel')]
                .find((r) => r.querySelector(`.edited[data-channel="${c}"]`));
            const all = [...(row?.querySelectorAll('select option') ?? [])]
                .map((o) => o.value)
                .filter((v) => v !== '' && v !== was);

            return all.find((v) => v.endsWith('.patch')) ?? all[0] ?? '';
        }, { c: chan, was: chosen });

        if (another !== '')
        {
            await page.evaluate(() => window.solo.pollChanParams());
            await page.focus('#params input[type="range"]');
            await page.keyboard.press('ArrowRight');
            await page.waitForFunction(
                (c) => !document.querySelector(
                    `#channels .edited[data-channel="${c}"]`).hidden,
                chan, { timeout: 60000 });

            await page.selectOption(
                `#channels .channel:has(.edited[data-channel="${chan}"]) ` +
                'select', another);
            await page.waitForFunction(
                () => /on channel \d+\. Play\.$/.test(
                    document.getElementById('status').textContent),
                null, { timeout: 60000 });
            await page.evaluate(() => window.solo.settled());

            check(await edited() === false,
                  `and choosing ${another} over an edited patch leaves a ` +
                  'row that does not say edited');
        }
    }

    /* ---- the instrument as a graph ---- */

    /* The node editor on the solo page, which has no shared document: the
       files are whatever the page is playing, and an edit rewrites the
       .dsp in the text box and reloads it -- the canvas and the box being
       two views of one text, as they are in the desktop's editor. */
    await page.selectOption('#mode', 'patch');
    await page.evaluate(() =>
    {
        document.getElementById('nodeview').open = true;
    });

    await page.waitForFunction(() => window.solo.node()?.boxes > 0,
                               null, { timeout: 60000 });

    const graph = await page.evaluate(() => window.solo.node());
    const offered = await page.$$eval('#nodefile option',
                                      (os) => os.map((o) => o.value));

    check(graph.boxes > 0 && offered.length > 0,
          `${offered.join(', ')} drew ${graph.boxes} boxes`);

    /* And it opens at 1:1 rather than fitted. A patch is wide -- ts1 is
       1888 pixels of graph -- so fitting it into a page-width box halves
       every label, and half-size labels are a picture of a patch rather
       than a patch to work on. Fit is the button for the other question,
       and after it the whole graph is in the box. */
    const opened = await page.evaluate(() =>
    {
        const s = document.getElementById('nodescroll');

        return { wide: s.scrollWidth > s.clientWidth + 1,
                 box: s.clientHeight };
    });

    await page.click('#nodefit');
    await new Promise((r) => setTimeout(r, 500));

    const whole = await page.evaluate(() =>
    {
        const s = document.getElementById('nodescroll');

        return s.scrollWidth <= s.clientWidth + 1;
    });

    check(opened.wide && opened.box > 200 && whole,
          `it opens at 1:1 in a ${opened.box}-pixel box and scrolls, and ` +
          'Fit brings the whole graph in');

    /* A node with a plain number on it, clicked, and the number typed
       into. The canvas is asked where the box is: the layout is its
       own. */
    const node = await page.evaluate(() =>
    {
        const n = window.solo.node();

        for (let i = 0; i < n.boxes; i++)
        {
            const b = n.box(i);

            if (b.kind === 0 && b.settable)
                return b;
        }

        return null;
    });

    if (node === null)
        check(false, 'nothing in the patch has a value to set');
    else
    {
        await page.locator('#nodescroll').scrollIntoViewIfNeeded();

        const at = await page.$eval('#nodecanvas', (c) =>
        {
            const r = c.getBoundingClientRect();

            return { x: r.x, y: r.y };
        });

        await page.mouse.click(at.x + node.x + 8, at.y + node.y + 8);
        await page.waitForFunction(() => window.solo.node().selected >= 0,
                                   null, { timeout: 15000 });

        const was = await page.inputValue('#dsp');
        const field = await page.$('#nodeparams input');

        if (field === null)
            check(false, `${node.name} has nothing to type into`);
        else
        {
            const arg = await field.evaluate((i) => i.dataset.arg);

            await field.fill('0.234');
            await field.press('Enter');
            await page.waitForFunction(
                (before) =>
                    document.getElementById('dsp').value !== before,
                was, { timeout: 15000 }).catch(() => {});

            const now = await page.inputValue('#dsp');
            const line = now.split('\n').find(
                (l, i) => l !== was.split('\n')[i]);

            check(now !== was && /0\.234/.test(line ?? ''),
                  `setting ${node.name}.${arg} rewrote the .dsp: ` +
                  `${(line ?? '').trim()}`);
        }
    }

    for (const e of errors)
        check(false, `page error: ${e}`);
}
catch (e)
{
    check(false, `threw: ${e.message.split('\n')[0]}`);
}

await browser.close();
site.closeAllConnections();
site.close();

process.stdout.write(`\n${failures === 0
                          ? 'the solo page\'s keys, knobs, parameters, ' +
                            'composer view and instrument graph still ' +
                            'work\n'
                          : `${failures} failed\n`}`);
process.exitCode = failures;
