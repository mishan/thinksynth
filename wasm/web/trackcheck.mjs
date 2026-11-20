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
 * trackcheck.mjs -- the hand tracker, as the site serves it, finds hands.
 *
 *   node wasm/web/trackcheck.mjs [BUILD_DIR]
 *
 * In Chromium, a module worker loads MediaPipe's tracker and its model from
 * the build's mediapipe/ alone, as a camera's theremin will, and finds the
 * two hands in scripts/hid/hands.jpg: a pair of open hands and nothing
 * else, released to the public domain by the photographer, Judgefloro
 * (https://commons.wikimedia.org/wiki/File:JfHands_raised_worshipfvf.JPG),
 * set at half size on a plain ground, the size hands are to a camera across
 * a room. Nothing it asks for leaves this server. How well it tracks is
 * MediaPipe's; this is that what we ship runs, so its thresholds are low.
 * A build made without the model says so and passes.
 *
 * Exit status is the number of failures.
 */

import './headless.mjs';

import fs from 'node:fs';
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

if (!/^const KEPT = .*mediapipe\/hand_landmarker\.task/m.test(
    fs.readFileSync(path.join(build, 'sw.js'), 'utf8')))
{
    console.log('skip  this build has no hand model');
    process.exit(0);
}

const site = await serve(build, 0, '127.0.0.1', null);
const origin = `http://127.0.0.1:${site.address().port}`;
const browser = await chromium.launch();
const photo = fs.readFileSync(path.join(here, '..', '..', 'scripts', 'hid', 'hands.jpg'))
    .toString('base64');

try
{
    const context = await browser.newContext();
    const page = await context.newPage();
    const asked = [];

    context.on('request', (r) => asked.push(r.url()));
    await page.goto(`${origin}/index.html?panes=0`);

    const found = await page.evaluate(async ([origin, photo]) =>
    {
        const bitmap = await createImageBitmap(await (await fetch(
            `data:image/jpeg;base64,${photo}`)).blob());
        const source = `
            import { FilesetResolver, HandLandmarker } from '${origin}/mediapipe/vision_bundle.mjs';
            onmessage = async (e) => {
                try {
                    const files = await FilesetResolver.forVisionTasks('${origin}/mediapipe/wasm', true);
                    const tracker = await HandLandmarker.createFromOptions(files, {
                        baseOptions: { modelAssetPath: '${origin}/mediapipe/hand_landmarker.task',
                                       delegate: 'CPU' },
                        runningMode: 'IMAGE', numHands: 2,
                        minHandDetectionConfidence: 0.1, minHandPresenceConfidence: 0.1 });
                    const r = tracker.detect(e.data);
                    postMessage({ hands: r.landmarks.length,
                                  points: r.landmarks.map((h) => h.length) });
                } catch (err) { postMessage({ error: String(err) }); }
            };`;
        const worker = new Worker(URL.createObjectURL(
            new Blob([source], { type: 'text/javascript' })), { type: 'module' });

        return new Promise((done) =>
        {
            worker.onmessage = (e) => done(e.data);
            worker.onerror = (e) => done({ error: e.message });
            setTimeout(() => done({ error: 'no answer in 60 s' }), 60000);
            worker.postMessage(bitmap, [bitmap]);
        });
    }, [origin, photo]);

    check(!found.error && found.hands === 2 && found.points.every((n) => n === 21),
          'the tracker the site serves finds the photo\'s two hands, 21 points each',
          JSON.stringify(found));

    const away = asked.filter((u) => !u.startsWith(origin) && !/^(data|blob):/.test(u));

    check(away.length === 0, 'and nothing it asked for left this server', away.join(', '));
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
