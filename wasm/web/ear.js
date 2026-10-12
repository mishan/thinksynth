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
 * gen::breed and gen::grow ask how near a candidate sounds to a target:
 * a render of tens of milliseconds each, and a tuned edit some seconds.
 * The worklet has a few milliseconds a quantum, and the mirror draws the
 * composer view, so neither renders them. Each asks (thinkweb.cpp,
 * tw_ear_mode 2); the page brings the request here, and the answer goes
 * back to whichever asked (host.js).
 *
 * A request names its graphs by their texts' hashes, and the texts come
 * with the first request that names them (`texts') and are kept here, so
 * this realm loads no piece. The one other thing it needs is the sound
 * files, which the page sends here as it sends them to the others.
 *
 * Anything that goes wrong is said: a module that will not start, or a
 * request that throws, answers `failed', and the page answers the realms
 * for it -- a listening stage waiting on an answer that never comes would
 * stop for good.
 */

import createThinkWeb from './thinkweb.js';
import { apply } from './engine.js';

let M = null;
const early = [];

async function start ({ bytes, windowlen, sampleRate })
{
    M = await createThinkWeb({
        instantiateWasm: (imports, done) =>
        {
            WebAssembly.instantiate(bytes, imports)
                .then((r) => done(r.instance, r.module))
                .catch((e) => postMessage({ type: 'failed',
                    text: `ear: the wasm did not compile: ${e}` }));

            return {};
        },
        print: () => {},
        printErr: () => {},
    });

    M._tw_create(sampleRate, windowlen, 128);
    M._tw_silent();
    M._tw_ear_mode(1);

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

    if (m.type === 'texts')
    {
        for (const [id, text] of Object.entries(m.texts))
            M.ccall('tw_ear_text', null, ['string', 'string'], [id, text]);
        return;
    }

    if (m.type === 'hear')
    {
        const a = m.ask;
        let r;

        try
        {
            r = JSON.parse(M.ccall('tw_hear', 'string',
                ['string', 'string', 'string', 'string', 'string', 'string',
                 'string', 'string', 'number', 'string'],
                [a.dsp, a.effect, a.args, a.key, a.tdsp ?? '',
                 a.teffect ?? '', a.targs ?? '', a.file ?? '', a.tune,
                 a.from]));
        }
        catch (e)
        {
            postMessage({ type: 'log', text: `ear: ${e}` });
            r = { ok: false, distance: 0, tuned: '' };
        }

        postMessage({ type: 'heard', id: m.id, ...r });
        return;
    }

    /* A sound file, the one thing a request names rather than carries. */
    apply(M, m, {});
}

onmessage = (e) => receive(e.data);
onerror = (e) => postMessage({ type: 'failed', text: `ear: ${e.message}` });
