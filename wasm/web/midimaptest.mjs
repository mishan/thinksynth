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
 */

/*
 * midimaptest.mjs -- MIDI knobs learned onto the patch panel, on the page.
 *
 *   node wasm/web/midimaptest.mjs [BUILD_DIR]
 *
 * Two fake Web MIDI inputs, and analog.dsp, whose panel has sliders and a
 * waveform menu: a slider and the menu each learned from a knob, the panel
 * following the knob to the slider's end and onto the menu's entries, the
 * pedal never taken by learn, and the mappings there again after a
 * reload. Learn's rules are midimapcheck.mjs's; what a
 * position does to a control is the engine's (panelcheck).
 *
 * Exit status is the number of failures.
 */

import './headless.mjs';

import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { chromium } from 'playwright';

import { serve } from './serve.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const build = path.resolve(process.argv[2] ?? path.join(here, '..', '..', 'build-web'));
let failures = 0;

const check = (ok, what, detail = '') =>
{
    console.log(`${ok ? 'ok   ' : 'FAIL '} ${what}${ok || !detail ? '' : `: ${detail}`}`);
    failures += ok ? 0 : 1;
};

/* Two inputs; window.midiSend(id, bytes) plays one. */
function fake ()
{
    const access = { inputs: new Map(), outputs: new Map(), onstatechange: null };

    for (const id of ['k1', 'k2'])
        access.inputs.set(id, { id, name: id, type: 'input', state: 'connected',
                                onmidimessage: null });
    navigator.requestMIDIAccess = async () => access;
    window.midiSend = (id, bytes) =>
        access.inputs.get(id).onmidimessage?.({ data: new Uint8Array(bytes) });
}

const site = await serve(build, 0, '127.0.0.1', null);
const url = `http://127.0.0.1:${site.address().port}/index.html?panes=0`;
const browser = await chromium.launch(
    { args: ['--autoplay-policy=no-user-gesture-required'] });

const until = (page, f, arg) => page.waitForFunction(f, arg, { timeout: 5000 })
    .then(() => true, () => false);
const listed = (page) => page.$$eval('#midimaps li', (l) => l.map((i) => i.firstChild.textContent.trim()));

/* A control on the panel moved by hand, as a drag or a pick does. */
const move = (page, selector, value) => page.$eval(selector, (e, v) =>
{
    e.value = v;
    e.dispatchEvent(new Event('input', { bubbles: true }));
}, value);

/* A knob turned from `from' to `to' on an input. */
const turn = (page, id, cc, from, to) => page.evaluate(([id, cc, from, to]) =>
{
    for (let v = from; v !== to; v += Math.sign(to - from))
        window.midiSend(id, [0xb0, cc, v]);
    window.midiSend(id, [0xb0, cc, to]);
}, [id, cc, from, to]);

try
{
    const context = await browser.newContext();
    const page = await context.newPage();
    const errors = [];

    page.on('pageerror', (e) => errors.push(e.message));
    await page.addInitScript(fake);
    await page.goto(url);
    await page.click('#start');
    await page.waitForFunction(() => !document.getElementById('loadpiece').disabled,
                               null, { timeout: 60000 });
    await page.selectOption('#mode', 'patch');
    await page.selectOption('#patch', 'analog.dsp');
    await page.evaluate(() => window.solo.settled());
    await page.waitForSelector('#params [data-row="wave1"] select', { timeout: 30000 });
    await page.evaluate(() => document.getElementById('paramview').scrollIntoView());
    await page.click('#midi');

    const slider = await page.evaluate(() => window.solo.chanParams().rows
        .find((r) => r.kind === 0 && r.knob === '' && r.hi > r.lo));
    const range = `#params [data-row="${slider.id}"] input[type="range"]`;

    await page.click('#midilearn');
    await move(page, range, slider.lo);
    await turn(page, 'k1', 74, 0, 10);
    check(JSON.stringify(await listed(page)) === JSON.stringify([`Ch 1 CC 74 -> ${slider.id}`]),
          'Learn, a slider moved and a knob turned: the knob is listed against it',
          JSON.stringify(await listed(page)));

    await turn(page, 'k1', 74, 10, 127);
    check(await until(page, ([s, hi]) => Math.abs(document.querySelector(s).value - hi) < 1e-6,
                      [range, slider.hi]),
          'and turned all the way, the panel shows the slider at its end',
          await page.$eval(range, (e) => e.value));

    const menu = '#params [data-row="wave1"] select';

    await page.click('#midilearn');
    await turn(page, 'k2', 20, 0, 10);
    await move(page, menu, '1');
    await turn(page, 'k2', 20, 10, 100);
    check(await until(page, (s) => document.querySelector(s).value === '2', menu),
          'a menu learned from another input lands on its last entry from near the top',
          await page.$eval(menu, (e) => e.value));
    await turn(page, 'k2', 20, 100, 64);
    check(await until(page, (s) => document.querySelector(s).value === '1', menu),
          'and on the middle one from half way', await page.$eval(menu, (e) => e.value));

    await page.click('#midilearn');
    await move(page, range, slider.lo);
    await turn(page, 'k1', 64, 0, 127);
    await turn(page, 'k1', 64, 127, 0);
    check((await listed(page)).length === 2, 'the pedal is never learned as a knob');
    await page.click('#midilearn');

    check(errors.length === 0, `no page errors${errors.length ? ': ' + errors.join('; ') : ''}`);

    await page.reload();
    await until(page, () => document.querySelectorAll('#midimaps li').length > 0);
    check(JSON.stringify(await listed(page)) ===
          JSON.stringify([`Ch 1 CC 74 -> ${slider.id}`, 'Ch 1 CC 20 -> wave1']),
          'the mappings are there again after a reload', JSON.stringify(await listed(page)));

    await page.click('#midimaps li button');
    check(JSON.stringify(await listed(page)) === JSON.stringify(['Ch 1 CC 20 -> wave1']),
          'and Remove takes one away', JSON.stringify(await listed(page)));
    await context.close();
}
catch (e)
{
    check(false, `threw: ${e.message}`);
}
finally
{
    await browser.close();
    site.close();
}

console.log(`\n${failures} failure${failures === 1 ? '' : 's'}`);
process.exit(failures);
