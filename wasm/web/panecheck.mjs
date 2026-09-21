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
 * panecheck.mjs -- the tiled page, in a browser.
 *
 *   cd wasm/web && npm ci && npx playwright install chromium
 *   node panecheck.mjs [BUILD_DIR]
 *
 * The claim panes.js makes is that it moves things and changes nothing:
 * every pane is an element already in the page, every id survives, and
 * putting the layout away leaves the document it started from. That is a
 * claim about two states and the trip between them, so this harness
 * takes the page across the threshold and back and holds the far side
 * against a photograph of the near one.
 *
 * And the claim that is worth more than the layout: a pane nobody is
 * looking at does no work. canvasview.js stops asking for frames when it
 * is not visible, composerview.js gates on its box being open, and until
 * there was a tiler nothing anywhere proved either. `window.solo.drawing'
 * is what the page says about it, and it says it about the two canvases
 * that cost the most.
 *
 * What it does not do is open a synth for longer than it has to.
 * pagetest.mjs is the harness for what the page plays; this one is about
 * where the page puts it.
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

/* Wide enough for the tiler's own threshold, and narrow enough to be
   under it. panes.js asks for 60em and a pointer that is not a finger. */
const WIDE = { width: 1600, height: 900 };
const NARROW = { width: 560, height: 900 };

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
    process.stdout.write(`panecheck: no site in ${build}; build it first ` +
                         '-- see wasm/web/CMakeLists.txt.\n');
    process.exit(1);
}

const site = await serve(build, 0, '127.0.0.1', null);
const base = `http://127.0.0.1:${site.address().port}/index.html`;
const errors = [];

const browser = await chromium.launch(
    { args: ['--autoplay-policy=no-user-gesture-required'] });

/* What the document is, pane by pane: where each one sits among its
   siblings, what is in it, and how it is folded. Taken with the tiler off
   and taken again after it has been on, because "it puts the document
   back" is a claim about exactly this. */
const photograph = () => page.evaluate(() =>
    window.solo.panes().map((id) =>
    {
        const el = document.getElementById(id);
        const kids = [...el.parentElement.children];

        return [id, kids.indexOf(el), el.parentElement.tagName,
                [...el.querySelectorAll('[id]')].map((n) => n.id).join(' '),
                el.tagName === 'DETAILS' ? el.open : null];
    }));

let page = null;

try
{
    page = await browser.newPage({ viewport: NARROW });

    page.on('pageerror', (e) => errors.push(e.message));
    page.on('console', (m) =>
    {
        if (m.type() === 'error')
            errors.push(m.text());
    });

    /* Asked for, and refused by the screen: the fallback is the page as
       it is, and it is what every other harness here runs against. */
    await page.goto(`${base}?panes=1`);
    await page.waitForFunction(
        () => document.getElementById('range').textContent !== '');

    check(await page.evaluate(() => !document.body.classList.contains('tiled')),
          'a narrow window is the document, whatever the query string says');

    const before = await photograph();

    check(before.length > 0, `${before.length} panes in the catalog`);

    /* ---- and across the threshold ---- */

    await page.setViewportSize(WIDE);
    await page.waitForFunction(
        () => document.body.classList.contains('tiled'));

    const adopted = await page.evaluate(() =>
        window.solo.panes().map((id) =>
        {
            const el = document.getElementById(id);

            return [id, el.closest('#panes .pane')?.id ?? null,
                    [...el.querySelectorAll('[id]')].map((n) => n.id)
                        .join(' '),
                    el.tagName === 'DETAILS'
                        ? [el.open,
                           el.querySelector(':scope > summary').hidden]
                        : null];
        }));

    check(adopted.every(([id, host]) => host === `pane-${id}`),
          'every pane is in the layout, in a pane of its own');
    /* Every id survives, which is the promise that lets every other
       harness here -- and every module the page hands an element to -- go
       on asking for them by name. */
    check(adopted.every(([, , ids], i) => ids === before[i][3]),
          'and every id inside them is where it was');

    const folds = adopted.filter(([, , , d]) => d !== null);

    check(folds.length > 0 && folds.every(([, , , d]) => d[0] && d[1]),
          `the ${folds.length} boxes that fold are open with their ` +
          'summary hidden -- the pane\'s header is the disclosure now');

    /* The one thing the layout must not do to the document. */
    check(await page.evaluate(
              () => document.querySelectorAll('#keys .white').length) > 0,
          'the keys are drawn in their pane');

    /* ---- and back ---- */

    await page.setViewportSize(NARROW);
    await page.waitForFunction(
        () => !document.body.classList.contains('tiled'));

    const after = await photograph();

    check(JSON.stringify(after) === JSON.stringify(before),
          'a narrow window is the document again, in the same order, ' +
          'folded the way it was');

    check(await page.evaluate(
              () => document.getElementById('panes').children.length === 0),
          'and the tiler has nothing left in it');

    /* ---- the dividers ---- */

    await page.setViewportSize(WIDE);
    await page.waitForFunction(
        () => document.body.classList.contains('tiled'));

    /* A drag moves one fraction and leaves the rest of the tree alone,
       and what it moves is what the pointer moved: the arithmetic is in
       pixels for exactly that reason. */
    const bar = page.locator('#panes > .panebox > .panesplit').first();
    const was = await page.evaluate(() =>
    {
        const kids = [...document.querySelectorAll(
            '#panes > .panebox > :not(.panesplit)')];

        return kids.map((k) => Math.round(k.getBoundingClientRect().width));
    });

    const grip = await bar.boundingBox();

    await page.mouse.move(grip.x + grip.width / 2, grip.y + grip.height / 2);
    await page.mouse.down();
    await page.mouse.move(grip.x + grip.width / 2 - 120,
                          grip.y + grip.height / 2, { steps: 8 });
    await page.mouse.up();

    const now = await page.evaluate(() =>
        [...document.querySelectorAll('#panes > .panebox > :not(.panesplit)')]
            .map((k) => Math.round(k.getBoundingClientRect().width)));

    check(Math.abs((was[0] - now[0]) - 120) <= 2 &&
          Math.abs((now[1] - was[1]) - 120) <= 2,
          `a divider dragged 120 pixels moved 120 pixels: ` +
          `${was.join('/')} -> ${now.join('/')}`);

    /* And it will not take a pane below what the markup said it needs.
       The left column holds the graph, which asks for 400. */
    await page.mouse.move(grip.x + grip.width / 2 - 120,
                          grip.y + grip.height / 2);
    await page.mouse.down();
    await page.mouse.move(grip.x - 1200, grip.y + grip.height / 2,
                          { steps: 12 });
    await page.mouse.up();

    const floor = await page.evaluate(() =>
        Math.round(document.querySelector('#panes > .panebox > *')
                           .getBoundingClientRect().width));

    check(floor >= 400 && floor < 460,
          `and stops at the minimum the graph asked for: ${floor} of 400`);

    /* ---- and it is remembered ---- */

    const kept = await page.evaluate(() => window.solo.layout());

    await page.reload();
    await page.waitForFunction(
        () => document.body.classList.contains('tiled') &&
              document.getElementById('range').textContent !== '');

    check(JSON.stringify(await page.evaluate(() => window.solo.layout())) ===
              JSON.stringify(kept),
          'a reload opens on the layout somebody left');

    /* ---- the mode is availability, not the layout ---- */

    const inMode = () => page.evaluate(() =>
        window.solo.panes().filter(
            (id) => document.getElementById(`pane-${id}`)
                            .checkVisibility()));

    const patching = await inMode();

    await page.selectOption('#mode', 'piece');
    await page.waitForFunction(
        () => document.getElementById('pane-roll').checkVisibility());

    const piecing = await inMode();

    check(!patching.includes('roll') && piecing.includes('roll') &&
          patching.includes('patchsource') &&
          !piecing.includes('patchsource'),
          'a mode switch takes its panes out of the layout and puts the ' +
          `other mode's in: ${patching.length} panes to ${piecing.length}`);

    await page.selectOption('#mode', 'patch');
    await page.waitForFunction(
        () => !document.getElementById('pane-roll').checkVisibility());

    check(JSON.stringify(await inMode()) === JSON.stringify(patching),
          'and switching back puts the first mode\'s panes where they were');

    /* ---- a pane nobody is looking at ---- */

    /* The frame loop, which is what a tiler is for on a page with two
       canvases in it. The graph is folded away in the document, so it is
       in the drawer rather than in the layout, and neither of them should
       be asking the mirror for anything in patch mode. */
    await page.click('#start');
    await page.waitForFunction(
        () => !document.getElementById('loadpiece').disabled,
        null, { timeout: 60000 });

    const quiet = await page.evaluate(() => window.solo.drawing());

    check(!quiet.composer,
          'the piece\'s picture asks for no frames in patch mode');

    await page.selectOption('#mode', 'piece');
    await page.waitForFunction(() => window.solo.drawing().composer,
                               null, { timeout: 60000 });
    check(true, 'and asks for them again when its pane is in front');

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
    ? 'the tiler moves the page and changes nothing\n'
    : `${failures} failed\n`}`);
process.exitCode = failures;
