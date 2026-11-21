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
 * camworker.js -- a webcam's frames played as a controller, off the page's
 * thread.
 *
 * camera.js draws each frame small and posts its luma here; the module's
 * camera (twcamera.cpp) reads it and says which notes to press and release
 * and how much each zone moved. Or, started as a theremin, it posts the
 * hands its tracker found, and the module's hands (twhands.cpp) say the
 * notes, their bends and aux values, and where each hand is. Its own
 * realm, and its own instance of the module, because the worklet's budget
 * is the audio's and the mirror's fast-forwards would hold frames up for
 * seconds.
 */

import createThinkWeb from './thinkweb.js';

let M = null, theremin = false;
const early = [];

async function start ({ bytes, mode, snap })
{
    theremin = mode === 'theremin';
    try
    {
        M = await createThinkWeb({
            instantiateWasm: (imports, done) =>
            {
                WebAssembly.instantiate(bytes, imports)
                    .then((r) => done(r.instance, r.module))
                    .catch((e) => postMessage({ type: 'failed',
                        text: `the camera's module did not compile: ${e}` }));

                return {};
            },
            print: () => {},
            printErr: () => {},
        });
    }
    catch (e)
    {
        postMessage({ type: 'failed', text: `the camera's module: ${e}` });
        return;
    }

    const err = theremin ? M.ccall('hd_start', 'string', ['number'], [snap ? 1 : 0])
                         : M.ccall('tc_start', 'string', [], []);

    if (err !== '')
    {
        postMessage({ type: 'failed', text: `the camera's profile: ${err}` });
        return;
    }

    for (const m of early.splice(0))
        receive(m);
}

function receive (m)
{
    if (m.type === 'start')
    {
        start(m);
        return;
    }

    /* Only the newest frame waits for the module: a burst of old ones
       would strike notes long gone. */
    if (M === null)
    {
        if (m.type === 'frame' || m.type === 'hands')
            early.splice(0, early.length, ...early.filter((e) => e.type !== m.type));
        early.push(m);
        return;
    }

    let said;

    if (m.type === 'frame' && !theremin)
    {
        /* The pointer first: making room may grow the heap, and HEAPU8
           read before that would be the old one. */
        const at = M._tc_luma(m.width, m.height);

        M.HEAPU8.set(new Uint8Array(m.luma), at);
        said = M._tc_feed(m.width, m.height, m.seconds);
    }
    else if (m.type === 'hands' && theremin)
    {
        const points = new Float32Array(m.points).subarray(0, m.count * 44);
        const at = M._hd_hands(m.count);

        M.HEAPF32.set(points, at >> 2);
        said = M._hd_feed(m.count, m.seconds);
    }
    else if (m.type === 'gone')
        said = theremin ? M._hd_gone() : M._tc_gone();
    else
        return;

    postMessage({ type: 'played', generation: m.generation,
                  ...JSON.parse(M.UTF8ToString(said)) });
}

onmessage = (e) => receive(e.data);
