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
 * ear.js -- the module again, in a worker, hearing for the others.
 *

/*
 * camera.js -- a webcam played as a controller, onto the page's keys.
 *
 * Moving a hand in one of nine zones of the picture strikes a note, as loud
 * as the zone was struck: the camera's own profile, an air harp
 * (hid/thhCamera.h), played by camworker.js. The notes go to the page's own
 * press and release, as a MIDI key's do (midi.js), so they play wherever
 * the keys play; the profile's channel is the page's.
 *
 * Each frame is drawn into a small canvas and its luma posted to the
 * worker, stamped with when the camera captured it where the browser says,
 * so the motion it reads is in camera time and not the page's.
 *
 * A feed that stops -- a hidden tab, a camera taken by another app -- sends
 * no frame to say so, and a zone struck when it stopped would hold its note
 * for good. After STALL_MS with no frame the worker is told the camera is
 * gone, which lets every note go; the next frame starts afresh.
 */

import { wasmBytes } from './host.js';

/* The width frames are drawn at, as the desktop's reader decodes them
   (camera/thiCamera.h): enough for thMotion's zones, and little to post. */
const WIDTH = 160;

/* thMotion's MAX_GAP_SECONDS. */
const STALL_MS = 250;

/* True where the page can ask for a camera and hear of each frame. */
export function cameraAvailable ()
{
    return typeof navigator !== 'undefined' &&
           typeof navigator.mediaDevices?.getUserMedia === 'function' &&
           typeof HTMLVideoElement !== 'undefined' &&
           'requestVideoFrameCallback' in HTMLVideoElement.prototype;
}

/* A button that opens the camera and closes it again, a line beside it
 * saying how it went, and nine cells that light with the zones. The button
 * is left disabled for the page to enable once there is a synth to play.
 *
 * Returns { forget }, which is openCamera's forget on whatever is open.
 */
export function cameraToggle ({ button, status, zones, onNoteOn, onNoteOff })
{
    const label = button.textContent;
    const cells = [...zones.children];
    let open = null;

    const shut = (why = '') =>
    {
        open?.close();
        open = null;
        button.textContent = label;
        status.textContent = why;
        zones.hidden = true;
    };

    button.addEventListener('click', async () =>
    {
        if (open !== null)
        {
            shut();
            return;
        }

        button.disabled = true;
        status.textContent = 'asking...';

        /* The worker can fail before the camera has finished opening. */
        let failed = null;

        try
        {
            open = await openCamera({
                onNoteOn, onNoteOff,
                onZones: (levels) =>
                {
                    for (let z = 0; z < cells.length; z++)
                        cells[z].style.opacity = String(0.15 + 0.85 * Math.min(1, levels[z] * 2));
                },
                onFailed: (why) =>
                {
                    if (open === null)
                        failed = why;
                    else
                        shut(why);
                },
            });
        }
        catch (e)
        {
            status.textContent = e.message;
            button.disabled = false;
            return;
        }

        button.disabled = false;
        if (failed !== null)
        {
            shut(failed);
            return;
        }

        button.textContent = `${label}: on`;
        status.textContent = 'strike the zones';
        zones.hidden = false;
    });

    return { forget: () => open?.forget() };
}

/* Asks for a camera and plays it. `onNoteOn(note, velocity)' and
 * `onNoteOff(note)' are called once per note, `onZones(levels)' with the
 * nine zones' levels for every frame, and `onFailed(why)' if the camera or
 * the worker goes for good, after every note has been let go.
 *
 * Resolves to { forget, close }. `forget' drops the notes held without
 * calling onNoteOff, for a page that has just released everything itself.
 * `close' lets go of everything held and lets the camera go.
 *
 * Rejects with a message fit to put on the page.
 */
export async function openCamera ({ onNoteOn, onNoteOff, onZones = () => {},
                                    onFailed = () => {} })
{
    let stream;

    try
    {
        stream = await navigator.mediaDevices.getUserMedia({
            audio: false,
            video: { width: { ideal: 640 }, height: { ideal: 480 },
                     frameRate: { ideal: 120 } },
        });
    }
    catch (e)
    {
        if (e.name === 'NotAllowedError' || e.name === 'SecurityError')
            throw new Error('camera access was refused');
        if (e.name === 'NotFoundError' || e.name === 'OverconstrainedError')
            throw new Error('no camera');
        if (e.name === 'NotReadableError')
            throw new Error('the camera is in use');
        throw new Error(`the camera: ${e.message}`);
    }

    let bytes;

    try
    {
        bytes = (await wasmBytes()).slice(0);
    }
    catch (e)
    {
        for (const t of stream.getTracks())
            t.stop();
        throw new Error(`the camera's module: ${e.message}`);
    }

    const [track] = stream.getVideoTracks();
    const video = document.createElement('video');

    video.muted = true;
    video.playsInline = true;
    video.srcObject = stream;

    const worker = new Worker(new URL('camworker.js', import.meta.url),
                              { type: 'module' });
    const held = new Set();
    let open = true, last = performance.now(), stalled = false, frame = 0;
    let canvas = null, draw = null, watch = null;

    const letGo = () =>
    {
        for (const note of [...held])
        {
            held.delete(note);
            onNoteOff(note);
        }
    };

    const close = () =>
    {
        if (!open)
            return;

        open = false;
        clearInterval(watch);
        video.cancelVideoFrameCallback(frame);
        track.onended = null;
        for (const t of stream.getTracks())
            t.stop();
        worker.terminate();
        letGo();
    };

    const fail = (why) =>
    {
        close();
        onFailed(why);
    };

    worker.onmessage = (e) =>
    {
        const m = e.data;

        if (m.type === 'failed')
        {
            fail(m.text);
            return;
        }
        if (!open)
            return;

        for (const [note, velocity] of m.notes)
            if (velocity > 0 && !held.has(note))
            {
                held.add(note);
                onNoteOn(note, velocity);
            }
            else if (velocity === 0 && held.delete(note))
                onNoteOff(note);

        onZones(m.zones);
    };
    worker.onerror = (e) => fail(`the camera's worker: ${e.message ?? 'failed'}`);

    worker.postMessage({ type: 'start', bytes }, [bytes]);

    const onFrame = (now, meta) =>
    {
        if (!open)
            return;

        if (canvas === null)
        {
            canvas = document.createElement('canvas');
            canvas.width = WIDTH;
            canvas.height = Math.max(3, Math.round(WIDTH * meta.height / meta.width));
            draw = canvas.getContext('2d', { willReadFrequently: true });
        }

        const { width, height } = canvas;

        draw.drawImage(video, 0, 0, width, height);

        const rgba = draw.getImageData(0, 0, width, height).data;
        const luma = new Uint8Array(width * height);

        for (let i = 0, p = 0; i < luma.length; i++, p += 4)
            luma[i] = (77 * rgba[p] + 150 * rgba[p + 1] + 29 * rgba[p + 2]) >> 8;

        last = performance.now();
        stalled = false;
        worker.postMessage({ type: 'frame', luma: luma.buffer, width, height,
                             seconds: (meta.captureTime ?? now) / 1000 },
                           [luma.buffer]);
        frame = video.requestVideoFrameCallback(onFrame);
    };

    watch = setInterval(() =>
    {
        if (!stalled && performance.now() - last > STALL_MS)
        {
            stalled = true;
            worker.postMessage({ type: 'gone' });
        }
    }, STALL_MS / 2);

    track.onended = () => fail('the camera went away');

    try
    {
        await video.play();
    }
    catch (e)
    {
        close();
        throw new Error(`the camera would not play: ${e.message}`);
    }

    frame = video.requestVideoFrameCallback(onFrame);

    return {
        forget ()
        {
            held.clear();
            worker.postMessage({ type: 'gone' });
        },

        close,
    };
}
