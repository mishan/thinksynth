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
 * THESE TWO PAGES tiled, and nothing about tiling in general.
 * mullion's own suite is the other half: what a divider's arithmetic is,
 * what a chord does, what a drawer is for and that a split fills the box
 * it is in are claims about the tiler, made over its fixture page. When
 * one of those fails the tiler broke; when one of these fails, this
 * application did.
 *
 * So what is left here is what only these documents can say.
 *
 * That the layout moves things and changes nothing: every pane is an
 * element already in the page, every id survives, and putting the layout
 * away leaves the document it started from. The fixture makes that claim
 * too, but it is a claim about a document and these are the documents
 * that matter -- pagetest.mjs and jamtest.mjs are written against them
 * untiled, and it is this that keeps them honest.
 *
 * That a pane nobody is looking at does no work, said about the two
 * canvases that cost the most: canvasview.js stops asking for frames
 * when it is not visible and composerview.js gates on its box being
 * open, each a wasm instance drawing a picture a frame.
 *
 * And the rest of what this page does with a layout: the chrome that
 * became one strip, the popovers beside a node graph, the box the status
 * line means by "see below", the room page's own catalog, and a phone's
 * layout, where a pane has to keep its tab and there is no Alt 0.
 *
 * What it does not do is open a synth for longer than it has to.
 * pagetest.mjs is the harness for what the page plays; this one is about
 * where the page puts it.
 *
 * Exit status is the number of failures.
 */

/* Under scripts/headless.sh unless THINK_TEST_HEADLESS=0: see headless.mjs. */
import './headless.mjs';

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

    check(await page.evaluate(
              () => !document.body.classList.contains('tiled')),
          'and a narrow one is the document whatever the query string says');

    /* Patch mode, for everything below: the page opens on a sequence
       now, and what this harness is about is a mode with a graph in it --
       the two canvases that share a leaf, and a pane of its own to switch
       away from. The mode-switch check further down says which modes it
       means. */
    await page.selectOption('#mode', 'patch');

    /* And the verbs a pane has, asked of a page that has never tiled:
       there is no layout yet to raise one in and no drawer to put one
       into, because the page is the document it always was. Asking anyway
       is quiet rather than an error -- whatever calls them does not know
       which side of the threshold it is on. */
    check(await page.evaluate(() =>
          {
              try
              {
                  window.solo.pane('present', 'detail');
                  window.solo.pane('close', 'detail');

                  return window.solo.layout() === null &&
                         document.getElementById('detail').isConnected &&
                         document.getElementById('panes').children.length
                             === 0;
              }
              catch
              {
                  return false;
              }
          }),
          'and raising or closing a pane there is quiet, not an error');

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

    /* ---- the mode is availability, not the layout ---- */

    /* Back over the threshold, and from here on this harness is about
       this page rather than about the tiler: what a divider's arithmetic
       is, what a chord does and what a drawer is for are mullion's own
       suite's, and none of them needed a synth to say. */
    await page.setViewportSize(WIDE);
    await page.waitForFunction(
        () => document.body.classList.contains('tiled'));

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

    /* ---- and the chrome, which is one strip ---- */

    /* Every line across the top is a line the layout does not get, so
       what is up there has to be earning it. A synth is started once and
       the button that did it cannot do anything else; a piece's
       description belongs to the mode that has pieces in it. */
    /* What stands above the layout and costs it height. By the box and
       not by `hidden', since the overlay the popovers live in is a body
       child of no size and is not a line of anything. */
    const strip = () => page.evaluate(() =>
        [...document.querySelectorAll('body > *')]
            .filter((el) => el.id !== 'panes' && el.checkVisibility() &&
                            el.getBoundingClientRect().height > 0)
            .map((el) => el.id || el.tagName.toLowerCase()));

    check(await page.evaluate(
              () => !document.getElementById('start').checkVisibility()),
          'Start goes once there is a synth to have started');

    check(JSON.stringify(await strip()) === JSON.stringify(['chrome']),
          `and patch mode's chrome is the one strip: ${
              (await strip()).join(', ')}`);

    const quiet = await page.evaluate(() => window.solo.drawing());

    check(!quiet.composer && !quiet.seq,
          'the piece\'s picture and its tracks ask for no frames in ' +
          'patch mode');

    await page.selectOption('#mode', 'piece');

    /* Piece mode opens on the sequencer, with the piece's picture the
       tab behind it, so it is the tracks that start asking for frames --
       and the two of them share a leaf, which is the whole point: one
       picture is drawn and not two. */
    await page.waitForFunction(() => window.solo.drawing().seq,
                               null, { timeout: 60000 });

    check(!await page.evaluate(() => window.solo.drawing().composer),
          'the tracks ask for frames when their pane is in front, and the ' +
          'picture behind them does not');

    await page.click('#panetab-composerview');
    await page.waitForFunction(() => window.solo.drawing().composer,
                               null, { timeout: 60000 });

    check(!await page.evaluate(() => window.solo.drawing().seq),
          'and raising the picture turns the tracks off');

    /* ---- two canvases, one leaf ---- */

    /* Which is the whole of the performance argument for tabs. Both of
       these are a wasm instance drawing a frame a frame: stacked, one of
       them stops, and the page pays for one picture rather than two. */
    await page.click('#pane-composerview .panebody');
    await page.getByRole('button', { name: 'Patch graph', exact: true })
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

    /* ---- and the tracks in the mode they are for ---- */

    /* A sequence is a pane of its own in a layout of its own, so the
     * pane is raised the moment the mode switches and onShow says so.
     *
     * Which is where it went wrong. Every other caller asks the page
     * whether it is composing -- a sequence and a piece are the same
     * scheduler, and the tracks are for both -- and this one asked
     * whether the mode was `piece'. It is not, in the mode the tracks
     * exist for, so showing the pane turned them off and the grids
     * stopped redrawing under the pointer. Piece mode above cannot see
     * it: there the two answers agree.
     */
    await page.selectOption('#mode', 'seq');
    await page.waitForFunction(() => window.solo.tracks().length > 0,
                               null, { timeout: 60000 });

    check(await page.evaluate(() => window.solo.drawing().seq),
          'and the tracks draw in the mode they are for, not only in ' +
          'piece mode');

    /* Put away and raised again, which is the call the mode switch does
       not make: pickMode turns the tracks on itself after the layout has
       settled, so it papered over the callback being wrong. Closing the
       pane and presenting it is onShow and nothing else. */
    await page.evaluate(() => window.solo.pane('close', 'seqview'));
    await page.waitForFunction(() => !window.solo.drawing().seq,
                               null, { timeout: 30000 });

    await page.evaluate(() => window.solo.pane('present', 'seqview'));

    check(await page.evaluate(async () =>
          {
              await new Promise((go) => requestAnimationFrame(go));

              return window.solo.drawing().seq;
          }),
          'and a sequence pane put away and raised again goes back to ' +
          'drawing');

    await page.selectOption('#mode', 'piece');
    await page.waitForFunction(() => window.solo.drawing().composer,
                               null, { timeout: 60000 });

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

    /* ---- and closing one beside it ---- */

    /* A leaf keeps which of its tabs is in front as an index, and closing
       a tab before that one moves everything after it up. Three tabs with
       the middle one in front, and the first closed: what was in front is
       still in front, one place to its left. */
    /* Put up as one leaf rather than closed and presented: a pane
       presented goes back where it was, which is three leaves. */
    const was = await page.evaluate(() =>
    {
        const before = window.solo.pane('layout');

        window.solo.pane('setLayout',
                         { tabs: ['keyboard', 'paramview', 'detail'],
                           active: 1 });
        window.solo.pane('close', 'keyboard');

        return before;
    });
    await page.waitForTimeout(150);

    check(await page.evaluate(() =>
              document.getElementById('pane-paramview').checkVisibility() &&
              !document.getElementById('pane-detail').checkVisibility()),
          'closing the tab before the one in front leaves it in front');

    await page.evaluate((before) => window.solo.pane('setLayout', before),
                        was);
    await page.waitForTimeout(150);

    /* ---- and the box a message points at ---- */

    /* "See below" is a document's sentence: the box is under the message
     * and opening its fold is the whole of it. Tiled, the fold is open
     * already and held that way, and what is between the message and the
     * box is another tab in front of it -- or the drawer, if somebody
     * closed the pane. A page that says see below and shows nothing is
     * worse than one that says nothing.
     *
     * And it does not take the focus doing it: the .dsp that did not
     * parse is read by whoever was editing it, and the box is put beside
     * their text rather than over it.
     */
    await page.evaluate(() => document.activeElement.blur());
    await page.keyboard.press('Alt+Digit0');
    await page.waitForTimeout(200);
    await page.evaluate(() => window.solo.pane('close', 'detail'));
    await page.waitForTimeout(150);

    await page.click('#dsp');
    await page.evaluate(() =>
    {
        /* Pressed rather than clicked, so that what the focus does next
           is the layout's doing and not the pointer's. */
        document.getElementById('dsp').value = 'this is not a patch {';
        document.getElementById('load').click();
    });
    await page.evaluate(() => window.solo.settled());
    await page.waitForTimeout(250);

    check(await page.evaluate(() =>
              document.getElementById('pane-detail').checkVisibility() &&
              document.getElementById('pane-patchsource')
                      .checkVisibility()),
          'a .dsp that did not parse raises the box the status line ' +
          'points at, and not over the source it came from');

    check(await page.evaluate(
              () => document.activeElement.closest('#dsp') !== null),
          'and leaves the caret where the reader left it');

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

    /* A layout left under the store's name before the site's was put in
       front of it: the one that opens, and moved rather than left. */
    await room.setViewportSize(WIDE);
    await room.evaluate((panes) =>
    {
        localStorage.clear();
        localStorage.setItem('panes:jam:room',
                             JSON.stringify({ tabs: panes }));
    }, await room.evaluate(() => window.jam.panes()));
    await room.reload();
    await room.waitForFunction(
        () => document.body.classList.contains('tiled'));

    const moved = await room.evaluate(() =>
        ({ split: document.querySelector('#panes .panesplit') !== null,
           keys: Object.keys(localStorage)
                     .filter((k) => k.includes('panes')) }));

    check(!moved.split,
          'a layout saved under the old store name is the one that opens');

    check(moved.keys.length === 1 &&
          moved.keys[0] === 'thinksynth:panes:jam:room',
          `and it moves to the new name: ${moved.keys.join(' ')}`);

    /* ---- a phone ----
     *
     * Only the keys go without a tab strip. A phone tiled with every lone
     * leaf stripless left a pane somebody had moved into a leaf of its
     * own with nothing to drag or close it by, and no Alt 0 to start
     * over with: this is that layout, as it was kept, and the button that
     * takes the place of the chord. */
    const touch = await browser.newContext({
        viewport: { width: 412, height: 915 }, isMobile: true,
        hasTouch: true });
    const phone = await touch.newPage();

    phone.on('pageerror', (e) => errors.push(`phone: ${e.message}`));

    await phone.goto(`${base}?phone=1`);
    await phone.waitForFunction(() => window.solo?.settled !== undefined);
    await phone.evaluate(() => localStorage.setItem(
        'thinksynth:panes:touch:patch', JSON.stringify(
            { dir: 'col', size: [0.2, 0.3, 0.2, 0.3], kids: [
                { tabs: ['detail'] }, { tabs: ['patchsource'] },
                { tabs: ['paramview', 'nodeview'] },
                { tabs: ['keyboard'] }] })));
    await phone.reload();
    await phone.waitForFunction(() => window.solo?.settled !== undefined);
    await phone.selectOption('#mode', 'patch');
    await phone.evaluate(() => window.solo.settled());

    const strips = () => phone.evaluate(() => Object.fromEntries(
        [...document.querySelectorAll('#panes .paneleaf')].map((l) => [
            [...l.querySelectorAll('.panetab')]
                .map((t) => t.id.replace(/^panetab-/, '')).join('+'),
            getComputedStyle(l.querySelector('.panetabs')).display !==
                'none'])));
    const stuck = await strips();

    check(stuck.detail === true && stuck.patchsource === true &&
          stuck.keyboard === false,
          'a phone draws a strip over a pane alone in its leaf, and none ' +
          `over the keys: ${JSON.stringify(stuck)}`);

    await phone.tap('#menubutton');
    await phone.tap('#menureset');
    await phone.waitForTimeout(200);

    const reset = await strips();

    check(JSON.stringify(Object.keys(reset)) ===
          JSON.stringify(['paramview+nodeview', 'keyboard']),
          'and Reset layout, in the menu, puts the mode\'s own layout ' +
          `back: ${Object.keys(reset).join(', ')}`);

    /* And the panes put away are in the menu, not a row over the
       layout: one tap brings one back and closes the menu over it. */
    const menu = await phone.evaluate(() => ({
        open: document.getElementById('menu').open,
        row: document.querySelector('#panes .panedrawer') !== null,
        listed: [...document.querySelectorAll('#menupanes .paneclosed')]
            .map((b) => b.id.replace(/^panereopen-/, '')).join(' '),
        site: document.getElementById('site').closest('#menu') !== null,
    }));

    check(!menu.open && !menu.row && menu.listed === 'patchsource detail' &&
          menu.site,
          'a phone lists the closed panes in its menu and not over the ' +
          `layout, with the source link: ${menu.listed}`);

    await phone.tap('#menubutton');
    await phone.tap('#panereopen-detail');
    await phone.waitForTimeout(200);

    check(await phone.evaluate(() =>
              !document.getElementById('menu').open &&
              document.getElementById('panetab-detail') !== null),
          'and a pane tapped there is back, with the menu closed');

    await touch.close();

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
