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
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * phonebenchtest.mjs -- the phone's benchmark page (phonebench.html) run
 * through in Chromium on its fake camera, each measure a couple of
 * seconds and the heat run a few: its results are all there, the
 * camera's frames counted, the air harp's motion read from thinkhid, and
 * each of the tracker's delegates measured or saying why not.
 *
 *   node wasm/web/phonebenchtest.mjs [BUILD_DIR]
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

function check (cond, what, detail = '')
{
    process.stdout.write(`${cond ? 'ok   ' : 'FAIL '} ${what}` +
                         `${!cond && detail ? `: ${detail}` : ''}\n`);
    failures += cond ? 0 : 1;
}

const site = await serve(build, 0, '127.0.0.1', '');
const browser = await chromium.launch({
    args: ['--use-fake-device-for-media-stream', '--use-fake-ui-for-media-stream'] });

try
{
    const page = await browser.newPage();
    const errors = [];

    page.on('pageerror', (e) => errors.push(e.message));
    await page.goto(`http://127.0.0.1:${site.address().port}/phonebench.html?seconds=2&soak=0.05`);
    check(await page.waitForFunction(() =>
              /Ready/.test(document.getElementById('status').textContent))
              .then(() => true, () => false),
          'the page says it is ready once its script has run', await page.textContent('#status'));
    await page.check('#soak');
    await page.click('#start');
    /* The run's end, not a stage's error, which the run goes on past. */
    const finished = await page.waitForFunction(() =>
    {
        try
        {
            const r = JSON.parse(document.getElementById('results').textContent);

            return r.done === true || r.error !== undefined;
        }
        catch
        {
            return false;
        }
    }, null, { timeout: 240000 }).then(() => true, () => false);

    if (!finished)
        throw new Error(`it didn't finish: ${await page.textContent('#status')}\n` +
                        await page.textContent('#results'));

    const r = JSON.parse(await page.textContent('#results'));
    const delegate = (d) => d && (typeof d.error === 'string' ||
                                  (d.fps > 0 && d.loadMs > 0 && d.frameMs.count > 0));

    check(r.done === true && r.error === undefined, 'the run finishes', r.error);
    check(typeof r.device?.agent === 'string' && r.device.cores > 0,
          'it says what the device is', JSON.stringify(r.device));
    check(r.camera?.fps > 0 && r.camera.width > 0, 'it counts the camera\'s frames',
          JSON.stringify(r.camera));
    check(r.harp?.fps > 0 && r.harp.axes === 14 && r.harp.motionMs.count > 0 &&
          r.harp.readMs.count > 0,
          'the air harp\'s frames are read, and thMotion\'s 14 axes made of each',
          JSON.stringify(r.harp));
    check(delegate(r.tracker?.GPU) && delegate(r.tracker?.CPU),
          'the tracker is measured on the GPU and the CPU, or says why not',
          JSON.stringify(r.tracker));
    check(typeof r.soak?.error === 'string' || r.soak?.perMinute?.length > 0,
          'the heat run reports by the minute, or says why not', JSON.stringify(r.soak));
    check(await page.isVisible('#copy'), 'and the results can be copied');

    /* Run again, without the heat run: none of the first run's is left. */
    await page.uncheck('#soak');
    await page.click('#start');
    await page.waitForFunction((was) =>
    {
        try
        {
            const r = JSON.parse(document.getElementById('results').textContent);

            return r.done === true && r.when !== was;
        }
        catch
        {
            return false;
        }
    }, r.when, { timeout: 240000 });

    const again = JSON.parse(await page.textContent('#results'));

    check(again.soak === undefined && again.batteryAfter === undefined,
          'a second run has its own results, with no heat run left from the first',
          JSON.stringify(Object.keys(again)));
    check(errors.length === 0, `no page errors${errors.length ? ': ' + errors.join('; ') : ''}`);

    /* A module the page imports that doesn't load: the page says so,
       rather than a Start that does nothing. */
    const unloaded = await browser.newPage();

    await unloaded.route('**/thinkhid.js', (route) => route.fulfill({ status: 404, body: '' }));
    await unloaded.goto(`http://127.0.0.1:${site.address().port}/phonebench.html`);
    check(await unloaded.waitForFunction(() => /did not load/.test(
              document.getElementById('status').textContent), null, { timeout: 10000 })
              .then(() => true, () => false),
          'a script that doesn\'t load is said', await unloaded.textContent('#status'));
    await unloaded.close();

    /* A browser whose offscreen canvas throws when asked for WebGL 2, as
       some Safaris do: the run goes on and says it has none. */
    const safari = await browser.newPage();

    await safari.addInitScript(() =>
    {
        OffscreenCanvas.prototype.getContext = () => { throw new Error('not supported'); };
    });
    await safari.goto(`http://127.0.0.1:${site.address().port}/phonebench.html?seconds=1`);
    await safari.click('#start');
    check(await safari.waitForFunction(() =>
              /"offscreenWebgl2": false/.test(document.getElementById('results').textContent) &&
              /Asking for the camera|camera,|air harp|tracker|Done/.test(
                  document.getElementById('status').textContent),
              null, { timeout: 30000 }).then(() => true, () => false),
          'a canvas that throws for WebGL 2 is said, and the run goes on',
          await safari.textContent('#status'));
    await safari.close();

    /* A tracker whose worker fails on its first frame: each delegate says
       so, and the run goes on to its end. */
    const broken = await browser.newPage();

    await broken.route('**/handworker.js', (route) => route.fulfill({
        contentType: 'text/javascript',
        body: `onmessage = (e) => {
                   if (e.data.type === 'start')
                       postMessage({ type: 'ready', delegate: e.data.delegate });
                   else
                       throw new Error('the tracker broke');
               };` }));
    await broken.goto(`http://127.0.0.1:${site.address().port}/phonebench.html?seconds=2&soak=0.05`);
    await broken.check('#soak');
    await broken.click('#start');
    await broken.waitForFunction(() => /"done": true|"error": "[^"]*stopped/.test(
        document.getElementById('results').textContent), null, { timeout: 120000 })
        .catch(() => {});

    const failed = JSON.parse(await broken.textContent('#results'));

    check(failed.done === true && /broke/.test(failed.tracker?.GPU?.error) &&
          /broke/.test(failed.tracker?.CPU?.error) && /neither/.test(failed.soak?.error),
          'a tracker that fails says so for each delegate, and the run finishes',
          JSON.stringify({ tracker: failed.tracker, soak: failed.soak, done: failed.done }));
}
catch (e)
{
    check(false, `threw: ${e.stack}`);
}

await browser.close();
site.close();
process.stdout.write(`\n${failures} failure${failures === 1 ? '' : 's'}\n`);
process.exit(failures);
