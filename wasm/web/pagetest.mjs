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
 * pagetest.mjs -- the solo page's hands and dials, in a browser.
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
    await page.click('#loadpiece');
    await page.waitForFunction(
        () => document.querySelectorAll('#knobs input').length > 0,
        null, { timeout: 60000 });

    const knobs = await page.evaluate(() =>
        [...document.querySelectorAll('#knobs input')].map((i) =>
            ({ id: i.id, knob: i.dataset.knob,
               shown: i.previousElementSibling.textContent })));

    check(knobs.length > 0 &&
          knobs.every((k) => k.shown !== '' && k.knob !== undefined),
          `${PIECE}'s knobs drew, each with its index and its value: ` +
          knobs.map((k) => `${k.id}=${k.shown}`).join(', '));

    /* A drag moves the number beside the slider, which is the span a
       remote peer's move writes to on the room page as well. */
    await page.fill(`#${knobs[0].id}`, '0.42');
    await page.dispatchEvent(`#${knobs[0].id}`, 'input');

    const shown = await page.evaluate(
        (id) => document.getElementById(id).previousElementSibling.textContent,
        knobs[0].id);

    check(shown === '0.420', `a drag moves the number beside it: ${shown}`);

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
                          ? 'the solo page\'s keys and knobs still work\n'
                          : `${failures} failed\n`}`);
process.exitCode = failures;
