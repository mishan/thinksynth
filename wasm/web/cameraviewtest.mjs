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
 * cameraviewtest.mjs -- the camera view on the page, through a fake camera.
 *
 *   node wasm/web/cameraviewtest.mjs [BUILD_DIR]
 *
 * A canvas streamed as the camera, a bright square on it: the Camera pane
 * offered only while the camera is on, the picture drawn mirrored as the
 * zones are, the zone the square moves in shaded, and a strike's ring
 * drawn round it. Read back from the view's own pixels. What a zone's
 * level and a strike are is camcheck.mjs's and scripts/hidcamcheck's.
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

/* A 640x480 camera: dark, with a square where window.fakeCamera.show(at)
   puts it, as fractions of the camera's own picture, three shades in turn
   while `move' so it keeps its zone moving. */
function fake ()
{
    const lens = document.createElement('canvas');
    let square = null, moving = false, n = 0;

    lens.width = 640;
    lens.height = 480;
    const paint = () =>
    {
        const g = lens.getContext('2d');

        g.fillStyle = '#282828';
        g.fillRect(0, 0, 640, 480);
        g.fillStyle = n++ % 2 ? '#383838' : '#282828';
        g.fillRect(0, 0, 4, 4);
        if (square !== null)
        {
            g.fillStyle = moving ? ['#dcdcdc', '#a0a0a0', '#646464'][n % 3] : '#dcdcdc';
            g.fillRect(square[0] * 640 - 60, square[1] * 480 - 60, 120, 120);
        }
    };

    window.fakeCamera = { show (at, move = false) { square = at; moving = move; } };
    navigator.mediaDevices.getUserMedia = async () =>
    {
        paint();
        setInterval(paint, 1000 / 60);
        return lens.captureStream(60);
    };
}

const site = await serve(build, 0, '127.0.0.1', null);
const url = `http://127.0.0.1:${site.address().port}/index.html`;
const browser = await chromium.launch(
    { args: ['--autoplay-policy=no-user-gesture-required'] });

/* The view's pixel at (x, y), as fractions of its picture: [r, g, b]. */
const pixel = (page, x, y) => page.$eval('#cameracanvas', (c, [x, y]) =>
    [...c.getContext('2d').getImageData(Math.round(x * c.width),
                                        Math.round(y * c.height), 1, 1).data].slice(0, 3),
    [x, y]);
/* Whether the view's pixel at `at' is ever `rgb' over `ms' of frames. */
const ever = (page, at, rgb, ms) => page.$eval('#cameracanvas', async (c, [[x, y], rgb, ms]) =>
{
    const g = c.getContext('2d');

    for (const end = performance.now() + ms; performance.now() < end;)
    {
        const p = g.getImageData(Math.round(x * c.width), Math.round(y * c.height), 1, 1).data;

        if (rgb.every((v, i) => Math.abs(p[i] - v) < 10))
            return true;
        await new Promise((go) => requestAnimationFrame(go));
    }
    return false;
}, [at, rgb, ms]);
const bright = ([r, g, b]) => r + g + b;
const blue = ([r, , b]) => b - r;

/* The bluest each of `points' is over `ms' of frames: a camera hands over
   the odd frame twice, and a frame its last one matches has no motion. */
const bluest = (page, points, ms) => page.$eval('#cameracanvas', async (c, [points, ms]) =>
{
    const g = c.getContext('2d');
    const best = points.map(() => [0, 0, 0]);

    for (const end = performance.now() + ms; performance.now() < end;)
    {
        points.forEach(([x, y], i) =>
        {
            const p = [...g.getImageData(Math.round(x * c.width), Math.round(y * c.height),
                                         1, 1).data].slice(0, 3);

            if (p[2] - p[0] > best[i][2] - best[i][0])
                best[i] = p;
        });
        await new Promise((go) => requestAnimationFrame(go));
    }
    return best;
}, [points, ms]);

try
{
    const page = await (await browser.newContext({ viewport: { width: 1400, height: 900 } })).newPage();
    const errors = [];
    const off = () => page.$eval('#cameraview', (e) => e.hasAttribute('data-pane-off'));

    page.on('pageerror', (e) => errors.push(e.message));
    await page.addInitScript(fake);
    await page.goto(url);
    await page.click('#start');
    await page.waitForFunction(() => !document.getElementById('loadpiece').disabled,
                               null, { timeout: 60000 });
    check(await off(), 'with no camera the Camera pane is not offered');

    await page.click('#camera');
    await page.waitForFunction(() => document.getElementById('camerastatus').textContent
        .includes('strike'), null, { timeout: 30000 });
    check(!await off(), 'with the camera on it is');
    check(await page.isVisible('#cameracanvas') && await page.isVisible('#camera'),
          'and it is raised beside the keys, not over the button that opened it');

    /* The camera's bottom right, which is the player's bottom left, zone 7:
       on the left of the view. */
    await page.evaluate(() => window.fakeCamera.show([5 / 6, 5 / 6]));
    await page.waitForTimeout(1500);

    const left = await pixel(page, 1 / 6, 5 / 6), right = await pixel(page, 5 / 6, 5 / 6);

    check(bright(left) > bright(right) + 200,
          'the picture is drawn mirrored: the camera\'s bottom right on the left',
          JSON.stringify([left, right]));

    /* Zone 7's interior beside the square, and a little inside its top
       edge: shaded alike while it moves, the edge ringed when it strikes. */
    const inside = [190 / 640, 345 / 480], edge = [190 / 640, 324 / 480];
    const still = await pixel(page, ...inside);

    await page.evaluate(() => window.fakeCamera.show(null));
    await page.waitForTimeout(500);
    await page.evaluate(() => window.fakeCamera.show([5 / 6, 5 / 6], true));

    const [[shaded, ringed], grey] = await Promise.all([
        bluest(page, [inside, edge], 400),
        ever(page, [30 / 640, 476 / 480], [109, 109, 109], 1200)]);

    check(blue(shaded) > blue(still) + 15, 'a zone moving is shaded',
          JSON.stringify([still, shaded]));
    check(blue(ringed) > blue(shaded) + 40, 'and a strike rings it',
          JSON.stringify([shaded, ringed]));
    check(grey, 'and greys its bar till it may strike again');

    await page.click('#camera');
    check(await off(), 'and with the camera off again the pane goes');
    check(errors.length === 0, `no page errors${errors.length ? ': ' + errors.join('; ') : ''}`);
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
