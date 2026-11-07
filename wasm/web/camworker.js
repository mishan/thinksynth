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
 * camworker.js -- a webcam's frames played as a controller, off the page's
 * thread.
 *
 * camera.js draws each frame small and posts its luma here; the module's
 * camera (twcamera.cpp) reads it and says which notes to press and release
 * and how much each zone moved. Its own realm, and its own instance of the
 * module, because the worklet's budget is the audio's and the mirror's
 * fast-forwards would hold frames up for seconds.
 */

import createThinkWeb from './thinkweb.js';

let M = null;
const early = [];

async function start ({ bytes })
{
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

    const err = M.ccall('tc_start', 'string', [], []);

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

    if (M === null)
    {
        early.push(m);
        return;
    }

    let said;

    if (m.type === 'frame')
    {
        M.HEAPU8.set(new Uint8Array(m.luma), M._tc_luma(m.width, m.height));
        said = M._tc_feed(m.width, m.height, m.seconds);
    }
    else if (m.type === 'gone')
        said = M._tc_gone();
    else
        return;

    postMessage({ type: 'played', ...JSON.parse(M.UTF8ToString(said)) });
}

onmessage = (e) => receive(e.data);
