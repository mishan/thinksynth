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
 * evolve.js -- one Evolve toward search, in a worker of its own.
 *
 * The desktop scores a generation on several threads; the browser has
 * none to give a module, so the page runs a search a worker each, on
 * seeds of their own, and keeps the best (host.js). A search here is one
 * call, minutes long, that hears nothing until it returns -- so it is
 * ended by ending the worker, and what it found on the way has gone out
 * as it found it: where it began, its commentary, and each graph nearer
 * than any before (thinkweb.cpp, tw_evolve).
 */

import createThinkWeb from './thinkweb.js';
import { apply } from './engine.js';

async function search ({ bytes, windowlen, sampleRate, spec, seed, sound,
                         sounds })
{
    const M = await createThinkWeb({
        instantiateWasm: (imports, done) =>
        {
            WebAssembly.instantiate(bytes, imports)
                .then((r) => done(r.instance, r.module))
                .catch((e) => postMessage({ type: 'done', ok: false,
                    why: `the wasm did not compile: ${e}` }));

            return {};
        },
        print: () => {},
        printErr: () => {},
    });
    const began = performance.now();

    M._tw_create(sampleRate, windowlen, 128);
    M._tw_silent();

    /* The page's sound files, and a recording, written where the module
       reads files. */
    for (const [name, bytes] of sounds ?? [])
        apply(M, { type: 'sample', name, bytes }, {});

    if (sound)
        apply(M, { type: 'sample', name: sound.name, bytes: sound.bytes }, {});

    M.onEvolve = (kind, value, text) =>
    {
        if (kind === 0)
            postMessage({ type: 'say', text });
        else if (kind === 1)
            postMessage({ type: 'started', distance: value });
        else
            postMessage({ type: 'nearer', distance: value, graph: text,
                          seconds: (performance.now() - began) / 1000 });
    };

    const r = JSON.parse(M.ccall('tw_evolve', 'string',
        ['string', 'string', 'string', 'string', 'string', 'number',
         'number', 'number'],
        [spec.start, spec.startArgs, spec.target, spec.targetArgs,
         sound ? `/dsp/${sound.name}` : '', spec.grow ? 1 : 0, seed,
         spec.generations ?? 0]));

    postMessage({ type: 'done', ...r });
}

onmessage = (e) =>
{
    search(e.data).catch((err) =>
        postMessage({ type: 'done', ok: false, why: String(err) }));
};
