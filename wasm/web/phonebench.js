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
 * phonebench.js -- how a phone plays its camera, measured on the phone
 * by the path its camera instruments will take: the camera's frame rate;
 * the air harp's work a frame, the frame drawn small and read as luma
 * and then thMotion in thinkhid, as camera.js and twcamera.cpp do it on
 * the page; and the hand tracker, handworker.js as the page runs it, on
 * the GPU and on the CPU, one frame at a time as camera.js sends them;
 * and, for heat, the faster of the two for ten minutes, its frame rate
 * and the battery each minute. The results go on the page as JSON, to
 * copy and send back.
 *
 * ?seconds=N runs each measure N seconds (20); ?soak=M the heat run M
 * minutes (10).
 */

import createThinkHid from './thinkhid.js';

const query = new URLSearchParams(location.search);
const SECONDS = Number(query.get('seconds')) || 20;
const SOAK_MINUTES = Number(query.get('soak')) || 10;

/* The width the air harp reads frames at, as camera.js draws them. */
const WIDTH = 160;

const $ = (id) => document.getElementById(id);
const say = (text) => { $('status').textContent = text; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const now = () => performance.now();

const results = { version: 1, when: new Date().toISOString() };

const show = () => { $('results').textContent = JSON.stringify(results, null, 2); };

/* Times, in ms, as a mean and the worst but one in twenty. */
function summary (ms)
{
    const sorted = [...ms].sort((a, b) => a - b);
    const round = (x) => Math.round(x * 100) / 100;

    return { count: ms.length,
             mean: round(ms.reduce((a, b) => a + b, 0) / Math.max(1, ms.length)),
             p95: round(sorted[Math.min(sorted.length - 1, Math.floor(sorted.length * 0.95))] ?? 0) };
}

function device ()
{
    const gl = document.createElement('canvas').getContext('webgl2');
    const info = gl?.getExtension('WEBGL_debug_renderer_info');

    return {
        agent: navigator.userAgent,
        cores: navigator.hardwareConcurrency ?? null,
        memory: navigator.deviceMemory ?? null,
        screen: `${screen.width}x${screen.height}@${devicePixelRatio}`,
        gpu: gl ? gl.getParameter(info ? info.UNMASKED_RENDERER_WEBGL : gl.RENDERER) : null,
        offscreenWebgl2: typeof OffscreenCanvas !== 'undefined' &&
                         new OffscreenCanvas(1, 1).getContext('webgl2') !== null,
    };
}

async function battery ()
{
    const b = await navigator.getBattery?.().catch(() => null);

    return b ? { level: b.level, charging: b.charging } : null;
}

/* Each frame the camera gives, for `seconds', to `each(meta)': the
   frames a second, or what `each' threw. */
function frames (video, seconds, each)
{
    return new Promise((resolve, reject) =>
    {
        const end = now() + seconds * 1000;
        let count = 0;
        const next = (ms, meta) =>
        {
            count++;
            try
            {
                each(meta);
            }
            catch (e)
            {
                reject(e);
                return;
            }
            if (now() < end)
                video.requestVideoFrameCallback(next);
            else
                resolve(count / seconds);
        };

        video.requestVideoFrameCallback(next);
    });
}

async function harp (video)
{
    const M = await createThinkHid();
    const height = Math.round(WIDTH * video.videoHeight / video.videoWidth);
    const canvas = Object.assign(document.createElement('canvas'), { width: WIDTH, height });
    const draw = canvas.getContext('2d', { willReadFrequently: true });
    const read = [], motion = [];
    let axes = 0;

    const fps = await frames(video, SECONDS, (meta) =>
    {
        const t0 = now();

        draw.drawImage(video, 0, 0, WIDTH, height);

        const rgba = draw.getImageData(0, 0, WIDTH, height).data;
        const luma = new Uint8Array(WIDTH * height);

        for (let i = 0, p = 0; i < luma.length; i++, p += 4)
            luma[i] = (77 * rgba[p] + 150 * rgba[p + 1] + 29 * rgba[p + 2]) >> 8;

        const t1 = now();
        const at = M._th_motion_luma(WIDTH, height);

        M.HEAPU8.set(luma, at);
        axes = JSON.parse(M.UTF8ToString(
            M._th_motion_feed(WIDTH, height, (meta.captureTime ?? t1) / 1000))).length;
        motion.push(now() - t1);
        read.push(t1 - t0);
    });

    return { fps: Math.round(fps * 10) / 10, size: `${WIDTH}x${height}`, axes,
             readMs: summary(read), motionMs: summary(motion) };
}

/* The tracker on `delegate' for `seconds', a frame at a time. */
async function tracker (video, delegate, seconds, minutes = null)
{
    const worker = new Worker(new URL('handworker.js', import.meta.url), { type: 'module' });
    const t0 = now();
    const ready = await new Promise((resolve) =>
    {
        worker.onmessage = (e) => resolve(e.data);
        worker.onerror = (e) => resolve({ type: 'failed', text: e.message ?? 'the worker failed' });
        worker.postMessage({ type: 'start', delegate });
    });

    if (ready.type !== 'ready')
    {
        worker.terminate();
        return { error: ready.text };
    }

    const loadMs = Math.round(now() - t0);
    const latency = [], perMinute = [];
    let id = 0, hands = 0, answered = 0, minute = now(), inMinute = 0;
    const end = now() + seconds * 1000;

    while (now() < end)
    {
        const sent = now();
        const bitmap = await createImageBitmap(video);
        const answer = await new Promise((resolve) =>
        {
            worker.onmessage = (e) => resolve(e.data);
            worker.postMessage({ type: 'frame', bitmap, seconds: sent / 1000,
                                 generation: 0, id: ++id }, [bitmap]);
        });

        latency.push(now() - sent);
        answered++;
        inMinute++;
        hands += answer.count > 0 ? 1 : 0;
        if (minutes !== null && now() - minute >= 60000)
        {
            perMinute.push({ fps: Math.round(inMinute / ((now() - minute) / 1000) * 10) / 10,
                             battery: await battery() });
            say(`Heat run: minute ${perMinute.length} of ${minutes}...`);
            minute = now();
            inMinute = 0;
        }
    }
    worker.terminate();
    /* And what there is of the last minute. */
    if (minutes !== null && inMinute > 0)
        perMinute.push({ fps: Math.round(inMinute / ((now() - minute) / 1000) * 10) / 10,
                         battery: await battery() });

    return { delegate: ready.delegate, loadMs,
             fps: Math.round(answered / seconds * 10) / 10,
             framesWithHands: hands, frameMs: summary(latency),
             ...(minutes !== null ? { perMinute } : {}) };
}

async function run ()
{
    $('start').disabled = true;
    $('copy').hidden = true;
    results.device = device();
    results.battery = await battery();
    show();

    let wake = null;

    try
    {
        wake = await navigator.wakeLock?.request('screen');
    }
    catch
    {
    }

    say('Asking for the camera...');

    const stream = await navigator.mediaDevices.getUserMedia({
        audio: false,
        video: { facingMode: 'user', width: { ideal: 640 }, height: { ideal: 480 },
                 frameRate: { ideal: 60 } },
    });
    const video = $('preview');

    video.srcObject = stream;
    video.hidden = false;
    await video.play();

    const settings = stream.getVideoTracks()[0].getSettings();

    say(`The camera, ${SECONDS} seconds...`);
    results.camera = { width: settings.width, height: settings.height,
                       asked: settings.frameRate ?? null,
                       fps: Math.round(await frames(video, SECONDS, () => {}) * 10) / 10 };
    show();

    say(`The air harp, ${SECONDS} seconds...`);
    results.harp = await harp(video).catch((e) => ({ error: e.message }));
    show();

    results.tracker = {};
    for (const delegate of ['GPU', 'CPU'])
    {
        say(`The hand tracker on the ${delegate}, ${SECONDS} seconds...`);
        results.tracker[delegate] = await tracker(video, delegate, SECONDS)
            .catch((e) => ({ error: e.message }));
        show();
    }

    if ($('soak').checked)
    {
        const best = ['GPU', 'CPU'].filter((d) => results.tracker[d].fps)
            .sort((a, b) => results.tracker[b].fps - results.tracker[a].fps)[0];

        if (best)
        {
            say(`Heat run on the ${best}: ${SOAK_MINUTES} minutes...`);
            results.soak = await tracker(video, best, SOAK_MINUTES * 60, SOAK_MINUTES)
                .catch((e) => ({ error: e.message }));
            results.batteryAfter = await battery();
            show();
        }
    }

    stream.getTracks().forEach((t) => t.stop());
    video.hidden = true;
    wake?.release().catch(() => {});
    results.done = true;
    show();
    say('Done. Copy the results and send them back.');
    $('copy').hidden = false;
    $('start').disabled = false;
}

$('start').addEventListener('click', () => run().catch((e) =>
{
    results.error = e.message;
    show();
    say(`It stopped: ${e.message}`);
    $('start').disabled = false;
}));
$('copy').addEventListener('click', async () =>
{
    try
    {
        await navigator.clipboard.writeText($('results').textContent);
        say('Copied.');
    }
    catch
    {
        getSelection().selectAllChildren($('results'));
        say('Select the results and copy them.');
    }
});
