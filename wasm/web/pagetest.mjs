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
 * and the knobs a piece declared as sliders (keyboard.js, knobs.js) --
 * were only ever exercised by hand, on the page a change to either is
 * most likely to break.
 *
 * Small on purpose: the octave, the sliders, a key down and up, and a key
 * typed into a text box, which must play nothing. What sounds is
 * browsertest.mjs's business and jamtest.mjs's; this is about the page.
 *
 * And then the composer view (JAM_M6.md, sections 4 to 6), which is the
 * one thing here with a whole second engine behind it: the piece's picture
 * is drawn by the mirror -- another instance of the module, in a worker,
 * fed the messages the worklet is fed -- and comes over as a list of ops
 * the page replays on a Canvas2D. What is checked is the round trip a
 * finger makes: the picture arrives and is replayed, a stage whose picture
 * is a control can be enlarged, a drag on it leaves as a command and comes
 * back as a board that has changed, and Escape puts it back. The transport
 * is left stopped for that, so that nothing but the drag could have
 * changed what is drawn.
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
const url = `http://127.0.0.1:${site.address().port}/index.html`;
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

    await page.selectOption('#mode', 'piece');
    await page.selectOption('#piece', PIECE);

    /* Choosing a piece loads it, and Load loads it again -- deliberately,
       since Load re-reads the .gen in the textarea, which is editable. So
       there are two loads here and the second replaces the sliders the
       first drew. Wait for each, or a drag can land on a slider that is
       about to be thrown away: nothing is asserted, everything passes,
       and only a machine fast enough to finish the second load in the
       80 ms after the first ever says so. */
    const drawn = await page.waitForSelector('#knobs input',
                                             { timeout: 60000 });

    await page.click('#loadpiece');
    await page.waitForFunction((was) =>
    {
        const now = document.querySelector('#knobs input');

        return now !== null && now !== was;
    }, drawn, { timeout: 60000 });

    const knobs = await page.evaluate(() =>
        [...document.querySelectorAll('#knobs input')].map((i) =>
            ({ id: i.id, knob: i.dataset.knob,
               shown: i.previousElementSibling.textContent })));

    check(knobs.length > 0 &&
          knobs.every((k) => k.shown !== '' && k.knob !== undefined),
          `${PIECE}'s knobs drew, each with its index and its value: ` +
          knobs.map((k) => `${k.id}=${k.shown}`).join(', '));

    /* Moving a slider moves the number beside it -- the span a remote
       peer's move writes to on the room page as well.
     *
       With the keyboard, because that is a real input event from the
       browser: fill() sets the value itself and synthesises one, which
       is a weaker claim about a range input and a poor one to debug.
       Both numbers go in the message, since a slider that did not move
       and a number that did not follow it are different bugs. */
    await page.focus(`#${knobs[0].id}`);

    const was = await page.inputValue(`#${knobs[0].id}`);

    await page.keyboard.press('ArrowRight');

    let now = await page.inputValue(`#${knobs[0].id}`);

    /* At the top of its range there is nowhere rightwards to go. */
    if (now === was)
    {
        await page.keyboard.press('ArrowLeft');
        now = await page.inputValue(`#${knobs[0].id}`);
    }

    const shown = await page.evaluate(
        (id) => document.getElementById(id).previousElementSibling.textContent,
        knobs[0].id);

    check(now !== was && shown === Number(now).toPrecision(3),
          `a nudge moves the slider and the number beside it: ` +
          `${was} -> ${now}, showing ${shown}`);

    /* A computer key holds an on-screen key and lets it go. The slider
       just dragged still has the focus, and a focused input is somewhere
       a key means typing. */
    await page.evaluate(() => document.activeElement?.blur());
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
                          ? 'the solo page\'s keys, knobs and composer ' +
                            'view still work\n'
                          : `${failures} failed\n`}`);
process.exitCode = failures;
