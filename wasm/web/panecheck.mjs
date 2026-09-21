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

/* A press, a move and a release over a target -- the layout's own
   gestures, which are pointer events and not the browser's drag: what is
   being moved is a box in a layout, and where it would land is drawn by
   the page rather than by a drag image. */
const drag = async (from, to, at = { x: 0.5, y: 0.5 }) =>
{
    const a = await page.locator(from).boundingBox();
    const b = await page.locator(to).boundingBox();

    await page.mouse.move(a.x + a.width / 2, a.y + a.height / 2);
    await page.mouse.down();
    await page.mouse.move(b.x + b.width * at.x, b.y + b.height * at.y,
                          { steps: 10 });
    await page.mouse.up();
    await page.waitForTimeout(100);
};

try
{
    page = await browser.newPage({ viewport: NARROW });

    page.on('pageerror', (e) => errors.push(e.message));
    page.on('console', (m) =>
    {
        if (m.type() === 'error')
            errors.push(m.text());
    });

    /* Refused by hand, in a window that would otherwise tile: the page is
       the document, which is what `?panes=0' is for and what every other
       harness here runs against. */
    await page.setViewportSize(WIDE);
    await page.goto(`${base}?panes=0`);
    await page.waitForFunction(
        () => document.getElementById('range').textContent !== '');

    check(await page.evaluate(
              () => !document.body.classList.contains('tiled') &&
                    document.getElementById('panes').children.length === 0),
          'a wide window with ?panes=0 is the document');

    /* And asked for, and refused by the screen: a finger cannot grab a
       divider and a 60em layout is four slivers in half that. */
    await page.setViewportSize(NARROW);
    await page.goto(`${base}?panes=1`);
    await page.waitForFunction(
        () => document.getElementById('range').textContent !== '');

    check(await page.evaluate(() => !document.body.classList.contains('tiled')),
          'and a narrow one is the document whatever the query string says');

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

    /* ---- two canvases, one leaf ---- */

    /* Which is the whole of the performance argument for tabs. Both of
       these are a wasm instance drawing a frame a frame: stacked, one of
       them stops, and the page pays for one picture rather than two. */
    await page.click('#pane-composerview .panebody');
    await page.getByRole('button', { name: 'The graph', exact: true })
              .click();
    await page.waitForFunction(() => window.solo.drawing().nodes,
                               null, { timeout: 60000 });

    const stacked = await page.evaluate(() => window.solo.drawing());

    check(!stacked.composer && stacked.nodes,
          'the graph raised over the piece\'s picture leaves one of them ' +
          'drawing, not two');

    check(await page.evaluate(() =>
              document.getElementById('pane-composerview')
                      .closest('.paneleaf') ===
              document.getElementById('pane-nodeview').closest('.paneleaf')),
          'and they are two tabs of one leaf');

    await page.click('#panetab-composerview');
    await page.waitForFunction(() => window.solo.drawing().composer,
                               null, { timeout: 60000 });

    const swapped = await page.evaluate(() => window.solo.drawing());

    check(swapped.composer && !swapped.nodes,
          'and raising the other one turns the first one off');

    /* ---- a tab dragged to the drawer, and back out of it ---- */

    await drag('#panetab-nodeview', '.panedrawer');
    await page.waitForFunction(() => !window.solo.drawing().nodes,
                               null, { timeout: 15000 });

    check(await page.evaluate(() =>
              [...document.querySelectorAll('.paneclosed')]
                  .some((b) => b.textContent === 'The graph')),
          'a tab dragged onto the drawer closes to it, drawing nothing');

    /* ---- and one dragged onto an edge, which is a split ---- */

    const splits = () => page.evaluate(() =>
    {
        const count = (n) => n.tabs !== undefined
            ? 0 : 1 + n.kids.reduce((a, k) => a + count(k), 0);

        return count(window.solo.layout());
    });

    const had = await splits();

    await drag('.panedrawer button:text-is("The graph")', '#pane-roll',
               { x: 0.92, y: 0.5 });

    check(await splits() === had + 1 &&
          await page.evaluate(() =>
              document.getElementById('pane-nodeview').closest('.paneleaf')
                      .parentElement.dataset.dir === 'row'),
          'and one dropped on a leaf\'s edge splits it that way');

    /* ---- driving it from the keys ---- */

    /* Every one of them is a chord with Alt in it, because on this page
       the bare letters are notes: keyboard.js binds Z-/ and Q-P, and a
       tiler that took W for itself would have taken a note. */
    const leafOf = (id) => page.evaluate((which) =>
    {
        const leaf = document.getElementById(`pane-${which}`)
                             .closest('.paneleaf');

        return [...document.querySelectorAll('.paneleaf')].indexOf(leaf);
    }, id);

    await page.click('#panetab-roll');

    const roll = await leafOf('roll');

    await page.keyboard.press('Alt+ArrowUp');

    const moved = await page.evaluate(() =>
        document.activeElement.id);

    check(moved !== 'panetab-roll' && moved.startsWith('panetab-'),
          `Alt and an arrow moves the focus to the pane that way: ${moved}`);

    /* And the pane itself, that way: into the leaf the arrow points at. */
    await page.click('#panetab-roll');
    await page.keyboard.press('Alt+Shift+ArrowUp');

    check(await leafOf('roll') !== roll,
          'Alt Shift and an arrow moves the pane rather than the focus');

    /* Zoom, which is what makes tiling bearable on a laptop: one leaf
       fills the layout and onShow fires for everything that left. */
    await page.click('#panetab-composerview');
    await page.waitForFunction(() => window.solo.drawing().composer,
                               null, { timeout: 30000 });
    await page.keyboard.press('Alt+Enter');
    await page.waitForTimeout(200);

    const alone = await page.evaluate(() =>
        [...document.querySelectorAll('#panes .paneleaf')].length);

    check(alone === 1 &&
          await page.evaluate(() =>
              document.getElementById('pane-composerview').checkVisibility()),
          'Alt Enter fills the layout with one pane and draws no others');

    await page.keyboard.press('Alt+Enter');
    await page.waitForTimeout(200);

    check(await page.evaluate(() =>
              document.querySelectorAll('#panes .paneleaf').length) > 1,
          'and again puts the rest back');

    /* Closed to the drawer, and the whole thing back to the default. */
    await page.click('#panetab-roll');
    await page.keyboard.press('Alt+KeyW');
    await page.waitForTimeout(200);

    check(await page.evaluate(() =>
              [...document.querySelectorAll('.paneclosed')]
                  .some((b) => b.textContent === 'Piano roll')),
          'Alt W closes a pane to the drawer');

    await page.keyboard.press('Alt+Digit0');
    await page.waitForTimeout(200);

    check(await page.evaluate(() =>
              document.getElementById('pane-roll').checkVisibility()),
          'and Alt 0 is the layout this page opens on');

    /* A chord typed into a text box is text. The source box is a pane of
       its own here, and W in it must be a W. */
    await page.click('#gen');
    await page.keyboard.press('Alt+KeyW');
    await page.waitForTimeout(200);

    check(await page.evaluate(() =>
              document.getElementById('pane-piecesource').checkVisibility()),
          'and none of them fires while the focus is in a text box');

    /* ---- the popovers ---- */

    /* Beside the box that asked for it and inside the window, which is
       the part that is new: a popover is placed in page coordinates
       beside a canvas, and a pane can be narrower than the popover's own
       maximum width. Off the right edge of a 60em document was rare; off
       the right edge of a 400-pixel pane is every time.
     *
     * And out of the pane it belongs beside, into the layout's overlay:
     * a pane is a box that scrolls, and a popover inside one is clipped
     * by it. */
    await page.selectOption('#piece', 'colony.gen');
    await page.waitForSelector('#composerstages button', { timeout: 60000 });
    await page.click('#panetab-composerview').catch(() => {});
    await page.waitForFunction(() => window.solo.drawing().composer,
                               null, { timeout: 30000 });

    check(await page.evaluate(() =>
              document.getElementById('composerparams').parentElement
                      .className === 'paneoverlay'),
          'a popover lives over the layout, not in the pane it points at');

    const canvas = await page.$eval('#composer', (c) =>
    {
        const r = c.getBoundingClientRect();

        return { x: r.x, y: r.y };
    });
    const handle = await page.evaluate(() => window.solo.handleOf(0, 0));

    if (handle.x < 0)
        check(false, 'the first stage has no params handle');
    else
    {
        await page.mouse.click(canvas.x + handle.x, canvas.y + handle.y);
        await page.waitForFunction(
            () => !document.getElementById('composerparams').hidden,
            null, { timeout: 15000 });

        const where = await page.evaluate(() =>
        {
            const r = document.getElementById('composerparams')
                              .getBoundingClientRect();

            return { left: r.left, right: r.right, top: r.top,
                     bottom: r.bottom, w: innerWidth, h: innerHeight };
        });

        check(where.left >= 0 && where.top >= 0 &&
              where.right <= where.w && where.bottom <= where.h,
              'and is inside the window, edge to edge: ' +
              `${Math.round(where.left)}-${Math.round(where.right)} ` +
              `of ${where.w}`);

        await page.mouse.click(canvas.x + 4, canvas.y + 4);
    }

    /* And the clamp with something to clamp: the node canvas is wider
       than any pane and scrolls, so a port can be put against the right
       edge of the window and right-clicked there. Beside the pointer
       would put the menu off the window; it does not go off the window. */
    await page.selectOption('#mode', 'patch');
    await page.waitForFunction(() => window.solo.node()?.boxes > 0,
                               null, { timeout: 60000 });
    await page.click('#pane-nodeview .panebody');
    await page.keyboard.press('Alt+Enter');
    await page.waitForTimeout(300);

    const port = await page.evaluate(() =>
    {
        const scroller = document.getElementById('nodescroll');
        const canvas = document.getElementById('nodecanvas');
        const node = window.solo.node();

        for (let i = 0; i < node.boxes; i++)
        {
            const box = node.box(i);
            const out = box.ports.find((p) => !p.isInput);

            if (out === undefined)
                continue;

            /* Scrolled so that this port sits a few pixels in from the
               right of the view, which here is the right of the window. */
            const want = out.x - (scroller.clientWidth - 10);

            if (want <= 0 ||
                want > scroller.scrollWidth - scroller.clientWidth)
                continue;

            scroller.scrollLeft = want;

            const r = canvas.getBoundingClientRect();

            return { name: box.name, port: out.name,
                     x: r.x + out.x, y: r.y + out.y, w: innerWidth };
        }

        return null;
    });

    if (port === null)
        check(false, 'no port in the patch can be put against the edge');
    else
    {
        await page.mouse.click(port.x, port.y, { button: 'right' });
        await page.waitForFunction(
            () => !document.getElementById('nodemenu').hidden,
            null, { timeout: 15000 }).catch(() => {});

        const menu = await page.evaluate(() =>
        {
            const m = document.getElementById('nodemenu');
            const r = m.getBoundingClientRect();

            return { up: !m.hidden, left: r.left, right: r.right,
                     w: innerWidth };
        });

        check(menu.up && menu.right <= menu.w && menu.left < port.x,
              `a menu asked for at ${Math.round(port.x)} of ${port.w} -- ` +
              `${port.name}.${port.port}, against the right of the window ` +
              `-- is held inside it: ` +
              `${Math.round(menu.left)}-${Math.round(menu.right)}`);

        await page.keyboard.press('Escape');
        await page.mouse.click(port.x - 200, port.y);
    }

    await page.keyboard.press('Alt+Enter');

    /* ---- a pane, asked for by name ---- */

    /* Which is the whole of what a pane is to anything outside this
       page: raise it, rename it, put it away. A window's four verbs, and
       the reason the layout can become one later without the tiler being
       told what a window is. */
    await page.evaluate(() => window.solo.pane('close', 'detail'));
    await page.waitForTimeout(150);

    check(await page.evaluate(() =>
              !document.getElementById('pane-detail').checkVisibility()),
          'a pane closed by name is put away');

    await page.evaluate(() => window.solo.pane('present', 'detail'));
    await page.waitForTimeout(150);

    check(await page.evaluate(() =>
              document.getElementById('pane-detail').checkVisibility()),
          'and presented by name is in front again');

    await page.evaluate(() =>
        window.solo.pane('setTitle', 'detail', 'What it is doing'));
    await page.waitForTimeout(150);

    check(await page.textContent('#panetab-detail') === 'What it is doing',
          'and its tab says what it was told to say');

    /* ---- and the room page, which is the same catalog again ---- */

    /* The two pages share most of their panes and all of their tiler.
       What is particular here is that the room's panes live inside a
       section that is hidden until somebody has joined, so the layout is
       built over a document nobody can see yet and has to be right when
       it appears. Joining wants a relay; being in the room is jamtest's
       business, and this is about the layout. */
    const room = await browser.newPage({ viewport: WIDE });

    room.on('pageerror', (e) => errors.push(`jam: ${e.message}`));

    await room.goto(`http://127.0.0.1:${site.address().port}/jam.html`);
    await room.waitForFunction(
        () => document.body.classList.contains('tiled'));
    await room.evaluate(() =>
    {
        document.getElementById('roompanel').hidden = false;
    });

    const inRoom = await room.evaluate(() =>
        window.jam.panes().map((id) =>
        {
            const el = document.getElementById(id);

            return [id, el.closest('#panes .pane')?.id ?? null,
                    el.closest('.paneleaf') !== null &&
                        el.checkVisibility()];
        }));

    check(inRoom.length > 0 &&
          inRoom.every(([id, host]) => host === `pane-${id}`),
          `the room page adopts its ${inRoom.length} panes the same way`);

    check(inRoom.filter(([, , up]) => up).length >= 5,
          `and its layout shows ` +
          `${inRoom.filter(([, , up]) => up).length} of them at once`);

    await room.setViewportSize(NARROW);
    await room.waitForFunction(
        () => !document.body.classList.contains('tiled'));

    check(await room.evaluate(() =>
              window.jam.panes().every(
                  (id) => document.getElementById(id).closest('#roompanel')
                          !== null) &&
              document.getElementById('panes').children.length === 0),
          'and a narrow window puts every one of them back in the room');

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
