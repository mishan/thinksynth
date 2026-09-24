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
 * keycheck.mjs -- who has the computer keyboard, in a browser.
 *
 *   cd wasm/web && npm ci && npx playwright install chromium
 *   node keycheck.mjs
 *
 * No module, no build directory, no audio: keyboard.js and keyfocus.js on
 * a page of controls, driven with real clicks and real key presses. That
 * is the whole of what decides whether a letter is a note, and it is
 * decided from what the browser reports about focus -- which is exactly
 * the part no harness that stubs a DOM would be testing.
 *
 * pagetest.mjs plays a key on the real page and proves the note reaches
 * the synth. This proves the rule around it: the list, the slider, the
 * number box and the text area, each clicked and each tabbed to, and
 * which of those leaves the keys with the instrument.
 *
 * Exit status is the number of failures.
 */

/* Under scripts/headless.sh unless THINK_TEST_HEADLESS=0: see headless.mjs. */
import './headless.mjs';

import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { chromium } from 'playwright';

const here = path.dirname(fileURLToPath(import.meta.url));

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

/* The page under test: the controls a real page has one of each of, and
   the two modules, imported as the page imports them. `window.keys'
   records what was played so a press can be asked about afterwards. */
const FIXTURE = `<!doctype html>
<html lang="en"><head><meta charset="utf-8"><title>keys</title></head>
<body>
<select id="list"><option value="a">a</option><option value="b">b</option></select>
<input id="slider" type="range" min="0" max="10" step="1" value="5">
<input id="number" type="number" value="1">
<button id="press">press</button>
<textarea id="text"></textarea>
<div id="editor" class="cm-editor"><div contenteditable="true" id="cm"></div></div>
<canvas id="canvas" tabindex="0" width="40" height="40"></canvas>
<span id="keysstate"></span>
<script type="module">
import { TypingKeys } from './keyboard.js';
import { createKeyFocus } from './keyfocus.js';

const played = [];
let released = 0;

const focus = createKeyFocus({
    editing: '.cm-editor',
    indicator: document.getElementById('keysstate'),
    onRelease: () => { released++; },
});

const keys = new TypingKeys({
    press: (note) => played.push(note),
    release: () => {},
    shifted: () => {},
    playable: () => true,
    focus,
});

window.addEventListener('keydown', (e) => keys.keyDown(e));
window.addEventListener('keyup', (e) => keys.keyUp(e));

window.keys = {
    played: () => played.slice(),
    clear: () => { played.length = 0; },
    released: () => released,
    active: () => document.activeElement?.id ?? '',
    state: () => document.getElementById('keysstate').textContent,
};
</script>
</body></html>
`;

const server = http.createServer((req, res) =>
{
    const name = new URL(req.url, 'http://localhost').pathname;

    if (name === '/' || name === '/index.html')
    {
        res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
        res.end(FIXTURE);
        return;
    }

    /* The two modules, from the source directory rather than from a
       build: nothing here is generated. */
    const file = path.join(here, path.basename(name));

    if (!/\.js$/.test(name) || !fs.existsSync(file))
    {
        res.writeHead(404);
        res.end();
        return;
    }

    res.writeHead(200, { 'Content-Type': 'text/javascript; charset=utf-8' });
    res.end(fs.readFileSync(file));
});

await new Promise((go) => server.listen(0, '127.0.0.1', go));

const url = `http://127.0.0.1:${server.address().port}/`;
const browser = await chromium.launch();
const page = await browser.newPage();

page.on('pageerror', (e) => check(false, `the page threw: ${e.message}`));

await page.goto(url);
await page.waitForFunction(() => window.keys !== undefined);

/* `z' is the lowest note of the typing layout; what it is worth does not
   matter here, only whether it arrived. */
const plays = async () =>
{
    await page.evaluate(() => window.keys.clear());
    await page.keyboard.press('z');

    return (await page.evaluate(() => window.keys.played().length)) > 0;
};

const active = () => page.evaluate(() => window.keys.active());
const state = () => page.evaluate(() => window.keys.state());
const released = () => page.evaluate(() => window.keys.released());

check(await plays(), 'with nothing focused, a letter is a note');

/* The bug this file was written for: choosing a patch left the focus on
   the list, and every letter after it went to the list instead of to the
   instrument, until something else was clicked. */
await page.click('#list');
await page.selectOption('#list', 'b');

check(await plays(),
      'after choosing from a list with the pointer, a letter is still a note');

check(await active() !== 'list',
      'and the list does not keep the focus it was clicked with');

/* And the other half of it: a list opened and dismissed never changes,
   so nothing lets go of it, and what keeps the letters is the rule and
   not the letting go. The Escape closes the browser's own popup, which
   has the keyboard while it is open -- inert keys belong to somebody
   reading a list of patches, and no rule here could or should take them
   back. The list still has the focus afterwards, and a letter is a note
   again rather than the popup's typeahead. */
await page.click('#list');
await page.keyboard.press('Escape');

check(await active() === 'list' && await plays(),
      'a list clicked and dismissed keeps the focus and leaves the keys');

/* A slider clicked with the pointer keeps its focus, because nothing is
   lost by it: the arrows go on nudging the slider and the letters go on
   being notes. */
await page.click('#slider');

const wasSlider = await page.evaluate(
    () => document.getElementById('slider').value);

check(await active() === 'slider' && await plays(),
      'a slider clicked with the pointer keeps the focus and leaves the keys');

await page.keyboard.press('ArrowRight');

const nudged = await page.evaluate(
    () => document.getElementById('slider').value);

check(Number(nudged) !== Number(wasSlider),
      `and its own arrows still reach it: ${wasSlider} -> ${nudged}`);

/* A box you type into is the exception, whichever way the focus got
   there: typing is what it is for. */
await page.click('#number');

check(!(await plays()), 'a number box clicked into is typing, not playing');

const beforeEscape = await released();

await page.keyboard.press('Escape');

check(await plays(), 'and Escape hands the keys back');

check(await released() > beforeEscape,
      'and says so, so whatever was held can be let go of');

await page.click('#text');
check(!(await plays()), 'a text area is typing');

await page.keyboard.press('Escape');
check(await plays(), 'and Escape leaves it');

await page.click('#cm');
check(!(await plays()), "the room page's code editor is typing too");

await page.keyboard.press('Escape');
check(await plays(), 'and Escape leaves that');

/* Tabbing is asking. A control the Tab key landed on keeps the letters --
   somebody navigating to a list means to type in it -- and Escape is how
   the instrument gets them back. */
await page.evaluate(() => document.getElementById('number').focus());
await page.keyboard.press('Tab');

const tabbed = await active();

check(tabbed === 'press', `Tab from the number box lands on the button`);

check(!(await plays()),
      `and a control tabbed to keeps the letters (${tabbed})`);

check(await state() !== '', 'and the page says whose the keys are');

await page.keyboard.press('Escape');

check(await plays() && await state() === '',
      'Escape gives them back, and the line goes quiet again');

/* A canvas is not a control that took the keys, and Escape on one is not
   this file's business: the composer view reads it to put an enlarged
   stage back. */
await page.click('#canvas');

check(await active() === 'canvas' && await plays(),
      'a canvas clicked on keeps its focus and leaves the keys alone');

const wasReleased = await released();

await page.keyboard.press('Escape');

check(await active() === 'canvas' && await released() === wasReleased,
      "and Escape on it is the canvas's, not this file's");

await browser.close();
server.close();

process.exitCode = failures;
