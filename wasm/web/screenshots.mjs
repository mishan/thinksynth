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
 * screenshots.mjs -- the browser pictures the README shows.
 *
 *   node screenshots.mjs [BUILD_DIR] [OUT_DIR]
 *
 * BUILD_DIR is the site (build-web/), OUT_DIR where the pictures go
 * (docs/screenshots/). The desktop's are scripts/screenshots.py's.
 *
 * A piece playing, at a desktop's size and on a phone. Chromium runs in shotbox's sealed() environment, so
 * the fonts are the system's and not whoever runs this; and under
 * scripts/headless.sh, like the tests, so the audio that clocks a piece
 * goes to a null sink.
 *
 *   web-piece.png   mirrorball.gen playing: the canvas, knobs and roll
 *   web-phone.png   the same piece on a phone, the roll in front
 */

/* Under scripts/headless.sh unless THINK_TEST_HEADLESS=0: see headless.mjs. */
import './headless.mjs';

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { chromium } from 'playwright';
import { sealed } from 'shotbox';

import { serve } from './serve.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const top = path.join(here, '..', '..');
const build = path.resolve(process.argv[2] ?? path.join(top, 'build-web'));
const out = path.resolve(process.argv[3] ??
                         path.join(top, 'docs', 'screenshots'));

if (!fs.existsSync(path.join(build, 'index.html')))
{
    process.stdout.write(`screenshots: no site in ${build}; build it first ` +
                         '-- see wasm/web/CMakeLists.txt.\n');
    process.exit(1);
}

fs.mkdirSync(out, { recursive: true });

const PIECE = 'mirrorball.gen';

/* Long enough for the roll to have a few bars of every part on it. */
const PLAY_MS = 20000;

const site = await serve(build, 0, '127.0.0.1', null);
const url = `http://127.0.0.1:${site.address().port}/`;
/* PULSE_SERVER through the seal: it is headless.sh's null sink. */
const seal = await sealed({ pass: ['PULSE_SERVER'] });
const browser = await chromium.launch({
    env: seal.env, args: ['--autoplay-policy=no-user-gesture-required'] });

const errors = [];

async function open (options, query = '')
{
    const context = await browser.newContext(options);
    const page = await context.newPage();

    page.on('pageerror', (e) => errors.push(e.message));
    await page.goto(url + query);
    await page.waitForFunction(
        () => document.getElementById('range').textContent !== '');

    await page.click('#start');
    await page.waitForFunction(
        () => !document.getElementById('loadpiece').disabled,
        null, { timeout: 60000 });
    await page.evaluate(() => window.solo.settled());

    return page;
}

/* Played, then stopped where it is, so the roll and the clock hold still
   while the picture is taken. */
async function play (page)
{
    await page.click('#play');
    await page.waitForTimeout(PLAY_MS);
    await page.click('#stop');
    await page.evaluate(() => window.solo.settled());
}

async function shoot (page, name)
{
    /* Nothing hovered, and nothing focused that draws a ring. */
    await page.mouse.move(0, 0);
    await page.evaluate(() => document.activeElement?.blur());
    await page.screenshot({ path: path.join(out, name) });
}

try
{
    const desk = await open({ viewport: { width: 1440, height: 900 } });

    await desk.selectOption('#mode', 'piece');
    await desk.selectOption('#piece', PIECE);
    await desk.evaluate(() => window.solo.settled());

    /* The canvas in front of the sequencer, which a piece with no grid
       tracks leaves empty. */
    await desk.evaluate(() => window.solo.pane('present', 'composerview'));
    await play(desk);
    await shoot(desk, 'web-piece.png');

    const phone = await open({
        viewport: { width: 412, height: 915 }, isMobile: true,
        hasTouch: true, deviceScaleFactor: 2 }, '?phone=1');

    await phone.selectOption('#mode', 'piece');
    await phone.selectOption('#piece', PIECE);
    await phone.evaluate(() => window.solo.settled());
    await play(phone);
    await shoot(phone, 'web-phone.png');
}
finally
{
    await browser.close();
    await seal.close();
    site.close();
}

if (errors.length > 0)
{
    process.stdout.write(`screenshots: the page threw:\n  ` +
                         errors.join('\n  ') + '\n');
    process.exit(1);
}

process.stdout.write(`screenshots: written to ${out}\n`);
