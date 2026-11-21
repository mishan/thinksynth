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
 * theremintest.mjs -- the camera played as a theremin, on the page.
 *
 *   node wasm/web/theremintest.mjs [BUILD_DIR]
 *
 * A canvas streamed as the camera, and in handworker.js's place a stub
 * that answers each frame with the hands the test puts in it (over a
 * BroadcastChannel), as slowly as a tracker might: a left pinch plays C3
 * through the page's keys, the right hand's height bends it and the left
 * hand's hushes it, as the Index's bends and aux go; one frame at a time
 * reaches the tracker, and one answer slower than the page waits for
 * leaves a frame extra a while rather than for good; a tracker that stops
 * answering lets the note go, as
 * does a switch to the air harp; and a build with no hand model says so.
 * What the hands play is twhands.cpp's (handscheck.mjs); the tracker is
 * trackcheck.mjs's.
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

/* handworker.js's place: ready at once, and each frame answered after 30
   ms, or `slow' ms once, with the hands last broadcast, each { x, y,
   pinched } as the player sees it, packed as handworker.js packs them.
   Says how many frames came, and how many came while one was unanswered. */
const STUB = `
let hands = [], frames = 0, overlapped = 0, pending = 0, quiet = false, slow = 0;
const say = new BroadcastChannel('stub');

say.onmessage = (e) =>
{
    if (e.data.hands)
        hands = e.data.hands;
    if ('quiet' in e.data)
        quiet = e.data.quiet;
    if (e.data.slow)
        slow = e.data.slow;
    if (e.data.ask)
        say.postMessage({ frames, overlapped });
};

function pack ()
{
    const f = new Float32Array(44 * hands.length);

    hands.forEach(({ x, y, pinched }, h) =>
    {
        const cx = 1 - x, cy = 1 - y, s = 0.1, o = h * 44;

        f.fill(cx, o, o + 21);
        f.fill(cy - 0.3 * s, o + 21, o + 42);
        f[o + 21] = cy + 0.4 * s;
        for (let k = 0; k < 4; k++)
        {
            f[o + 5 + 4 * k] = cx + (k - 1.5) * 0.1 * s;
            f[o + 21 + 5 + 4 * k] = cy - 0.1 * s;
        }
        f[o + 4] = cx - (pinched ? 0 : 0.06);
        f[o + 8] = cx + (pinched ? 0 : 0.06);
        f[o + 42] = -1;
    });
    return f;
}

onmessage = (e) =>
{
    if (e.data.type === 'start')
        postMessage({ type: 'ready' });
    if (e.data.type !== 'frame')
        return;
    e.data.bitmap.close();
    frames++;
    overlapped += pending > 0 ? 1 : 0;
    if (quiet)
        return;
    pending++;

    const wait = slow || 30;

    slow = 0;
    setTimeout(() =>
    {
        pending--;
        const points = pack();

        postMessage({ type: 'hands', points: points.buffer, count: hands.length,
                      seconds: e.data.seconds, generation: e.data.generation,
                      id: e.data.id },
                    [points.buffer]);
    }, wait);
};
`;

/* The camera: a canvas streamed, a flicker in a corner so its frames keep
   coming. And what the page posts the worklet: its notes, and a
   controller's actions. */
function fake ()
{
    const lens = document.createElement('canvas');
    let n = 0;

    lens.width = 640;
    lens.height = 480;
    navigator.mediaDevices.getUserMedia = async () =>
    {
        setInterval(() =>
        {
            const g = lens.getContext('2d');

            g.fillStyle = n++ % 2 ? '#303030' : '#282828';
            g.fillRect(0, 0, 640, 480);
        }, 1000 / 60);
        return lens.captureStream(60);
    };

    window.posted = [];

    const post = MessagePort.prototype.postMessage;

    MessagePort.prototype.postMessage = function (m, ...rest)
    {
        if (m?.type === 'on' || m?.type === 'off')
            window.posted.push(`${m.type} ${m.note}`);
        else if (m?.type === 'hid')
            window.posted.push(`hid ${m.kind} ${m.note} ${m.aux ?? ''} ${m.t?.toFixed(2)}`);
        return post.call(this, m, ...rest);
    };
}

const site = await serve(build, 0, '127.0.0.1', null);
const url = `http://127.0.0.1:${site.address().port}/index.html?panes=0`;
const browser = await chromium.launch(
    { args: ['--autoplay-policy=no-user-gesture-required'] });

const until = (page, f, arg, ms = 5000) => page.waitForFunction(f, arg, { timeout: ms })
    .then(() => true, () => false);
const posted = (page, text) => until(page, (t) => window.posted.includes(t), text);
const stub = (page, m) => page.evaluate((m) => new BroadcastChannel('stub').postMessage(m), m);
/* What the stub has seen, or a failure if no stub answers: a tracker the
   page has let go of can't. */
const stats = (page) => page.evaluate(() => new Promise((done, failed) =>
{
    const said = new BroadcastChannel('stub');
    const late = setTimeout(() => { said.close(); failed(new Error('no tracker answered')); }, 5000);

    said.onmessage = (e) =>
    {
        if (e.data.frames === undefined)
            return;
        clearTimeout(late);
        said.close();
        done(e.data);
    };
    said.postMessage({ ask: true });
}));

/* A page started on a patch, with the stub in handworker.js's place unless
   `real'. */
async function open (real = false)
{
    const page = await browser.newPage();
    const errors = [];

    page.on('pageerror', (e) => errors.push(e.message));
    if (!real)
        await page.route('**/handworker.js', (r) => r.fulfill(
            { contentType: 'text/javascript', body: STUB }));
    await page.addInitScript(fake);
    await page.goto(url);
    await page.click('#start');
    await page.waitForFunction(() => !document.getElementById('loadpiece').disabled,
                               null, { timeout: 60000 });
    await page.selectOption('#mode', 'patch');
    await page.evaluate(() => window.solo.settled());
    await page.selectOption('#cameramode', 'theremin');
    return { page, errors };
}

const status = (page) => page.textContent('#camerastatus');

try
{
    {
        const { page, errors } = await open();

        check(!await page.isHidden('#camerasnap'), 'Theremin picked, the snap switch is offered');
        await page.click('#camera');
        check(await until(page, () => /pinch your left hand/.test(
            document.getElementById('camerastatus').textContent)),
              'the camera opens as a theremin and says how to play it', await status(page));

        await stub(page, { hands: [{ x: 0.7, y: 0.2, pinched: false },
                                   { x: 0.3, y: 0.8, pinched: true }] });
        check(await posted(page, 'on 48'), 'a left pinch plays C3 through the page\'s keys',
              JSON.stringify(await page.evaluate(() => window.posted.slice(-6))));
        check(await posted(page, 'hid 3 48  7.00'),
              'the right hand a fifth of the way up bends it to the scale\'s step there',
              JSON.stringify(await page.evaluate(() => window.posted.filter((p) => p.startsWith('hid 3')).slice(-3))));
        check(await posted(page, 'hid 4 48 3 0.20'),
              'and the left hand 0.8 up hushes it by 0.2, on aux3',
              JSON.stringify(await page.evaluate(() => window.posted.filter((p) => p.startsWith('hid 4')).slice(-3))));

        await page.waitForTimeout(1000);

        const seen = await stats(page);

        check(seen.frames > 5 && seen.overlapped === 0,
              'one frame at a time reaches the tracker', JSON.stringify(seen));

        /* While the slow one is out, what is sent meanwhile overlaps it, as
           it would queue behind it in a tracker; after, nothing should. */
        await stub(page, { slow: 1500 });
        await page.waitForTimeout(5000);

        const later = await stats(page);

        check(later.frames - seen.frames > 40 && later.overlapped - seen.overlapped <= 10,
              'an answer slower than the page waits leaves a frame extra a while, not for good',
              JSON.stringify([seen, later]));

        /* A new camera, and a new tracker with it, told the hands again
           once it is up. */
        await page.uncheck('#camerasnap');

        let free = false;

        for (let t = 0; t < 25 && !free; t++)
        {
            await stub(page, { hands: [{ x: 0.7, y: 0.2, pinched: false },
                                       { x: 0.3, y: 0.8, pinched: true }] });
            free = await page.evaluate(() => window.posted.includes('hid 3 48  7.20'));
            await page.waitForTimeout(200);
        }
        check(free,
              'unsnapped, the camera opens afresh and bends freely: 0.2 of 36 semitones',
              JSON.stringify(await page.evaluate(() => window.posted.filter((p) => p.startsWith('hid 3')).slice(-3))));

        await page.evaluate(() => { window.posted.length = 0; });
        await stub(page, { quiet: true });
        check(await posted(page, 'off 48'), 'a tracker that stops answering lets the note go');

        await page.evaluate(() => { window.posted.length = 0; });
        await stub(page, { quiet: false });
        check(await posted(page, 'on 48'), 'and plays it again once it answers');
        await page.selectOption('#cameramode', 'harp');
        check(await posted(page, 'off 48') &&
              await until(page, () => /strike the zones/.test(
                  document.getElementById('camerastatus').textContent)),
              'switched to the air harp, it lets the note go and opens as the harp',
              await status(page));
        check(errors.length === 0, `no page errors${errors.length ? ': ' + errors.join('; ') : ''}`);
        await page.close();
    }

    {
        const { page } = await open();

        /* In the click's own task, so before the camera has opened. */
        await page.evaluate(() =>
        {
            const mode = document.getElementById('cameramode');

            document.getElementById('camera').click();
            mode.value = 'harp';
            mode.dispatchEvent(new Event('change'));
        });
        check(await until(page, () => /strike the zones/.test(
            document.getElementById('camerastatus').textContent)),
              'a mode picked while the camera opens is the one it opens as', await status(page));
        await page.close();
    }

    {
        const { page } = await open();

        await page.click('#camera');
        await until(page, () => /pinch your left hand/.test(
            document.getElementById('camerastatus').textContent));
        await stub(page, { quiet: true });
        check(await until(page, () => /stopped answering/.test(
            document.getElementById('camerastatus').textContent), null, 20000),
              'a tracker silent for good is let go of, not sent frames for ever',
              await status(page));
        await page.close();
    }

    {
        const { page } = await open(true);

        await page.route('**/mediapipe/hand_landmarker.task', (r) => r.fulfill({ status: 404 }));
        await page.click('#camera');
        check(await until(page, () => /isn't in this build/.test(
            document.getElementById('camerastatus').textContent), null, 30000),
              'a build with no hand model says the theremin isn\'t in it', await status(page));
        await page.close();
    }
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
