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
 * handworker.js -- a camera's frames to the hands in them, off the page's
 * thread.
 *
 * MediaPipe's hand tracker (Apache-2.0), served from the site's own
 * mediapipe/ and kept by the service worker on first use. camera.js hands
 * over one frame at a time, as an ImageBitmap, and makes the next only
 * once this one is answered, so a tracker slower than the camera drops
 * frames rather than queueing them. The answer is each hand as twhands.cpp
 * reads it: its 21 x, its 21 y, which hand the tracker says it is (0 Left,
 * 1 Right) and how sure, packed 44 floats a hand.
 *
 * Full URLs throughout: the tracker resolves a relative one against its own
 * file, not this one.
 */

const MEDIAPIPE = new URL('mediapipe/', import.meta.url).href;

/* How sure the tracker must be that a hand is there before it reports
   one: below its own half, since a player's hands across a room are
   smaller than the ones it was trained on. */
const SURE = 0.4;

let tracker = null, clock = 0;

/* WebGL drawn in software, as on a machine without a GPU, runs the tracker
   several times slower than its own CPU path does. */
function software ()
{
    const gl = new OffscreenCanvas(1, 1).getContext('webgl2');
    const info = gl?.getExtension('WEBGL_debug_renderer_info');

    return !gl || /SwiftShader|llvmpipe|software/i.test(
        gl.getParameter(info ? info.UNMASKED_RENDERER_WEBGL : gl.RENDERER));
}

async function start ()
{
    const model = `${MEDIAPIPE}hand_landmarker.task`;

    /* Only a missing model says so: offline, the service worker, which
       answers GETs alone, has it kept. */
    if ((await fetch(model, { method: 'HEAD' }).catch(() => null))?.status === 404)
    {
        postMessage({ type: 'failed', text: 'the theremin isn\'t in this build' });
        return;
    }

    try
    {
        const { FilesetResolver, HandLandmarker } =
            await import(`${MEDIAPIPE}vision_bundle.mjs`);
        const files = await FilesetResolver.forVisionTasks(`${MEDIAPIPE}wasm`, true);
        const make = (delegate) => HandLandmarker.createFromOptions(files, {
            baseOptions: { modelAssetPath: model, delegate },
            runningMode: 'VIDEO', numHands: 2,
            minHandDetectionConfidence: SURE, minHandPresenceConfidence: SURE,
        });

        tracker = await (software() ? make('CPU') : make('GPU').catch(() => make('CPU')));
    }
    catch (e)
    {
        postMessage({ type: 'failed', text: `the hand tracker: ${e.message ?? e}` });
        return;
    }
    postMessage({ type: 'ready' });
}

function frame ({ bitmap, seconds, generation, id })
{
    /* The tracker refuses a time that doesn't move on, and a camera can
       hand over two frames in one millisecond. */
    clock = Math.max(clock + 1, Math.round(seconds * 1000));

    let found;

    try
    {
        found = tracker.detectForVideo(bitmap, clock);
    }
    finally
    {
        bitmap.close();
    }

    const points = new Float32Array(44 * found.landmarks.length);

    found.landmarks.forEach((hand, h) =>
    {
        const side = found.handedness[h]?.[0];

        hand.forEach((p, i) =>
        {
            points[h * 44 + i] = p.x;
            points[h * 44 + 21 + i] = p.y;
        });
        points[h * 44 + 42] = side?.categoryName === 'Left' ? 0
                            : side?.categoryName === 'Right' ? 1 : -1;
        points[h * 44 + 43] = side?.score ?? 0;
    });
    postMessage({ type: 'hands', points: points.buffer, count: found.landmarks.length,
                  seconds, generation, id }, [points.buffer]);
}

onmessage = (e) =>
{
    if (e.data.type === 'start')
        start();
    else if (e.data.type === 'frame' && tracker !== null)
        frame(e.data);
    else
        e.data.bitmap?.close();
};
