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
    await page.check('#soak');
    await page.click('#start');
    const finished = await page.waitForFunction(() => /"done": true|"error"/.test(
        document.getElementById('results').textContent), null, { timeout: 240000 })
        .then(() => true, () => false);

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
    check(errors.length === 0, `no page errors${errors.length ? ': ' + errors.join('; ') : ''}`);
}
catch (e)
{
    check(false, `threw: ${e.stack}`);
}

await browser.close();
site.close();
process.stdout.write(`\n${failures} failure${failures === 1 ? '' : 's'}\n`);
process.exit(failures);
