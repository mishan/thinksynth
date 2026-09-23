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
 * pwatest.mjs -- the solo page installed, offline, and updated.
 *
 *   cd wasm/web && npm ci && npx playwright install chromium
 *   node pwatest.mjs [BUILD_DIR]
 *
 * The page registers its service worker over https only, and every other
 * harness here serves http, so none of them runs with one. This one asks
 * for it with `?sw', in Chromium, and checks the three things it is for:
 *
 *   installable  Chromium's own installability check has nothing to say
 *   offline      with the network gone, the page loads again, starts the
 *                synth -- the worklet, the module, the mirror worker -- and
 *                loads a patch it never fetched online
 *   updated      a changed sw.js installs beside the running version and
 *                waits; the next load activates it, loads again from it,
 *                and the old version's cache is gone. A deploy and one
 *                ordinary refresh are enough; and a page with the synth
 *                started offers Update rather than reloading under it; and
 *                a forced reload, past the worker, loads again from it
 *
 * And the room page is kept from the same cache, so that it runs the same
 * build as the worklet, mirror and wasm it shares with the solo page.
 *
 * It serves a copy of the site, the files sw.js lists and the few it does
 * not, because the update is made by rewriting sw.js.
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

/* One the page does not open on, so it is fetched only when picked. */
const PATCH = 'ebass.dsp';

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

const sw = path.join(build, 'sw.js');

if (!fs.existsSync(sw))
{
    process.stdout.write(`pwatest: no sw.js in ${build}; build it first ` +
                         '-- see wasm/web/CMakeLists.txt.\n');
    process.exit(1);
}

const FILES = JSON.parse(/^const FILES = (.*);$/m.exec(
    fs.readFileSync(sw, 'utf8'))[1]);

/* In the build directory rather than /tmp, which other jobs on a shared
   machine clean. */
const site = path.join(build, 'pwatest-site');

fs.rmSync(site, { recursive: true, force: true });

for (const rel of [...FILES, 'sw.js', 'config.json'])
{
    fs.mkdirSync(path.dirname(path.join(site, rel)), { recursive: true });
    fs.copyFileSync(path.join(build, rel), path.join(site, rel));
}

const server = await serve(site, 0, '127.0.0.1', null);
const origin = `http://127.0.0.1:${server.address().port}`;
const url = `${origin}/index.html?sw&panes=0`;

const errors = [];
const browser = await chromium.launch(
    { args: ['--autoplay-policy=no-user-gesture-required'] });

/* The page is up when the octave is drawn and the patch menu is filled,
   which init() does from dsp/index.json. */
const loaded = (page) => page.waitForFunction(
    () => document.getElementById('range').textContent !== '' &&
          document.getElementById('patch').options.length > 0,
    null, { timeout: 60000 });

const caches = (page) => page.evaluate(() => caches.keys());

/* What waitForFunction would be for an async predicate, which it does not
   await: the promise it gets back is truthy, and it returns at once. */
async function until (page, pred, timeout = 60000, arg = undefined)
{
    const end = Date.now() + timeout;

    /* A page that is loading again has no context to ask for a moment,
       and that is a no rather than a failure. */
    while (!await page.evaluate(pred, arg).catch((e) =>
               /context|navigat/i.test(e.message) ? false
                                                  : Promise.reject(e)))
    {
        if (Date.now() > end)
            throw new Error(`timed out after ${timeout} ms: ${pred}`);

        await new Promise((r) => setTimeout(r, 250));
    }
}

try
{
    const context = await browser.newContext();
    const page = await context.newPage();

    page.on('pageerror', (e) => errors.push(e.message));
    page.on('console', (m) =>
    {
        if (m.type() === 'error')
            errors.push(m.text());
    });

    /* ---- online, the first time ---- */

    await page.goto(url);
    await loaded(page);

    /* Claimed on activation, so this load is already the worker's and
       the cache is whole. */
    await page.waitForFunction(() => navigator.serviceWorker.controller,
                               null, { timeout: 60000 });

    const first = await caches(page);

    check(first.length === 1 && first[0].startsWith('thinksynth-'),
          `one cache, the site's: ${first.join(', ')}`);

    const cdp = await context.newCDPSession(page);
    const { installabilityErrors } =
        await cdp.send('Page.getInstallabilityErrors');

    check(installabilityErrors.length === 0,
          'Chromium would install it' +
          (installabilityErrors.length
              ? `: ${installabilityErrors.map((e) => e.errorId).join(', ')}`
              : ''));

    const manifest = await (await page.request.get(
        `${origin}/manifest.json`)).json();

    check(manifest.scope === './' && manifest.start_url === './',
          'the manifest is scoped to the directory it is served from');

    /* ---- offline ---- */

    await context.setOffline(true);

    const back = await page.reload().then(() => loaded(page))
        .then(() => true, () => false);

    check(back, 'offline, the page loads again');

    if (!back)
        throw new Error('no page offline, so nothing after it can be checked');

    await page.click('#start');
    await page.waitForFunction(
        () => !document.getElementById('loadpiece').disabled,
        null, { timeout: 60000 });
    check(true, 'offline, the synth starts: worklet, module and mirror');

    await page.selectOption('#mode', 'patch');
    await page.evaluate(() => window.solo.settled());
    await page.selectOption('#patch', PATCH);
    await page.evaluate(() => window.solo.settled());

    check(await page.$eval('#dsp', (e) => e.value) ===
              fs.readFileSync(path.join(site, 'dsp', PATCH), 'utf8'),
          `offline, a patch never fetched online loads: ${PATCH}`);

    /* Nothing to join offline, but the page itself comes from the cache,
       which is what keeps its bundle on the build its worklet is. */
    const room = await context.newPage();
    const reached = await room.goto(`${origin}/jam.html?sw`)
        .then(() => room.waitForSelector('#joinrow'))
        .then(() => true, () => false);

    check(reached, 'offline, the room page loads from the same cache');
    await room.close();

    /* ---- a new version ---- */

    await context.setOffline(false);

    const text = fs.readFileSync(path.join(site, 'sw.js'), 'utf8');
    const old = /^const VERSION = "(\w+)";$/m.exec(text)[1];
    const next = 'f'.repeat(old.length) === old ? '0'.repeat(old.length)
                                                : 'f'.repeat(old.length);

    fs.writeFileSync(path.join(site, 'sw.js'),
                     text.replace(`"${old}"`, `"${next}"`));

    await page.evaluate(async () =>
    {
        const reg = await navigator.serviceWorker.getRegistration();

        await reg.update();
    });

    await until(page, async () =>
        (await navigator.serviceWorker.getRegistration()).waiting !== null);

    check((await caches(page)).includes(`thinksynth-${old}`),
          'a new version installs and waits, the running one untouched');

    /* Not while a room page is open: it runs from the old cache too.
       Opened without `?sw', so that the one window asking is this one. */
    const open = await context.newPage();

    await open.goto(`${origin}/jam.html`);
    await open.waitForSelector('#joinrow');

    check(await open.evaluate(() =>
              navigator.serviceWorker.controller !== null),
          'the room page is the worker\'s too');

    await page.reload();
    await loaded(page);

    check(await page.evaluate(async () =>
              (await navigator.serviceWorker.getRegistration()).waiting
                  !== null) &&
          (await caches(page)).includes(`thinksynth-${old}`),
          'with the room page open, a load leaves it waiting');

    /* And says so: the worker refused, and the page offers the button
       rather than leaving somebody to guess why they are on an old
       build. */
    await until(page, () => !document.getElementById('update').hidden,
                30000).catch(() => {});

    check(!await page.evaluate(() => document.getElementById('update').hidden),
          'and offers Update, since the worker refused it');

    await open.close();

    /* The next load asks for it, is the one window, and so gets it --
       and loads a second time from it. */
    const again = page.waitForEvent('load', { timeout: 60000 })
        .then(() => page.waitForEvent('load', { timeout: 60000 }));

    await page.reload();
    await again;
    await loaded(page);

    const after = await caches(page);

    check(after.length === 1 && after[0] === `thinksynth-${next}`,
          `the next load activates it and the old cache goes: ` +
          after.join(', '));

    check(await page.evaluate(async () =>
              (await navigator.serviceWorker.getRegistration()).waiting
                  === null),
          'and nothing is left waiting');

    /* ---- a deploy, and one refresh ----
     *
     * What somebody actually does. The refresh is what finds the new
     * sw.js, and the new version then spends a few seconds fetching the
     * site before it waits -- so a page that asked only at load found it
     * still installing, and it took a second refresh to update. Now the
     * page asks once it is waiting, and nothing has been started, so the
     * one refresh is enough.
     */
    const version = (v) => fs.writeFileSync(path.join(site, 'sw.js'),
        text.replace(`"${old}"`, `"${v}"`));
    /* Handed its version rather than closing over it: a function goes to
       the page as its source, without the variables around it. */
    const only = (v) => until(page, async (want) =>
    {
        const keys = await caches.keys();

        return keys.length === 1 && keys[0] === `thinksynth-${want}`;
    }, 60000, v);

    const third = 'e'.repeat(old.length);

    version(third);
    await page.reload();

    check(await only(third).then(() => true, () => false),
          'a deploy and one ordinary refresh are enough to update');

    await loaded(page);

    /* ---- and a deploy while it plays ----
     *
     * Taking over reloads the page, and under somebody who has started
     * the synth that is what they were doing gone. So the new version
     * waits for a button instead, and the button is what updates.
     */
    await page.click('#start');
    await page.waitForFunction(
        () => !document.getElementById('loadpiece').disabled,
        null, { timeout: 60000 });

    const fourth = 'd'.repeat(old.length);

    version(fourth);
    await page.evaluate(async () =>
        (await navigator.serviceWorker.getRegistration()).update());

    const offered = await until(page,
        () => !document.getElementById('update').hidden)
        .then(() => true, () => false);

    check(offered && (await caches(page)).includes(`thinksynth-${third}`),
          'started, a new version is offered as Update and not taken');

    await page.click('#update');

    check(await only(fourth).then(() => true, () => false),
          'and Update takes it');

    await loaded(page);

    /* ---- a forced reload ----
     *
     * Past the worker for the page, and not for the mirror worker the
     * page starts, which the worker still serves: two builds, if a deploy
     * came between, and the mirror dies on its first call into the
     * module. So the page loads again, normally, and is the worker's.
     */
    const controlled = () => page.evaluate(() =>
        navigator.serviceWorker.controller !== null);

    const twice = page.waitForEvent('load', { timeout: 60000 })
        .then(() => page.waitForEvent('load', { timeout: 15000 }))
        .then(() => true, () => false);

    await cdp.send('Page.reload', { ignoreCache: true });

    const reloaded = await twice;

    await loaded(page);

    check(reloaded && await controlled(),
          'a forced reload loads again, and from the worker');

    /* And once: a browser that loads past the worker every time --
       DevTools' "Bypass for network" -- is not reloaded forever. */
    await cdp.send('Network.enable');
    await cdp.send('Network.setBypassServiceWorker', { bypass: true });

    let loads = 0;
    const count = () => loads++;

    page.on('load', count);
    await page.reload();
    await loaded(page);
    await page.waitForTimeout(3000);
    page.off('load', count);

    check(loads === 2 && !await controlled(),
          `bypassing the worker for good reloads once, not forever ` +
          `(${loads} loads)`);

    await cdp.send('Network.setBypassServiceWorker', { bypass: false });

    /* Only now, since a console error is exactly what an offline load
       that fell through to the network would print. */
    check(errors.length === 0,
          'no page errors' + (errors.length ? `: ${errors.join(' | ')}` : ''));

    await context.close();
}
finally
{
    await browser.close();
    server.close();
    fs.rmSync(site, { recursive: true, force: true });
}

process.stdout.write(`\n${failures} failure(s)\n`);
process.exit(failures);
