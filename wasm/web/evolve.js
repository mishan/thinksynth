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
 * evolve.js -- an Evolve toward search in a worker of its own, or one of
 * the listeners a search scores on.
 *
 * The desktop scores a generation on several threads, and the browser has
 * none to give a module. Where it can suspend one on a promise instead
 * (JSPI), the search runs here on a module built for that (thinkevolve.js)
 * and a generation is handed to the listeners -- this file again, on the
 * page's module, a worker each (`port') -- a candidate at a time to
 * whichever is free. Where it cannot, the page runs a search a worker each
 * on seeds of their own and keeps the best (host.js).
 *
 * A search is one call, minutes long, that hears nothing but its listeners
 * until it returns -- so it is ended by ending the workers, and what it
 * found on the way has gone out as it found it: where it began, its
 * commentary, and each graph nearer than any before (thinkweb.cpp,
 * tw_evolve).
 */

import { apply } from './engine.js';

/* The module at `url', from `bytes', with the page's sound files and a
   recording written where it reads files. */
async function start (url, { bytes, windowlen, sampleRate, sound, sounds },
                      failed)
{
    const { default: create } = await import(url);
    const M = await create({
        instantiateWasm: (imports, done) =>
        {
            WebAssembly.instantiate(bytes, imports)
                .then((r) => done(r.instance, r.module))
                .catch((e) => failed(`the wasm did not compile: ${e}`));

            return {};
        },
        print: () => {},
        printErr: () => {},
    });

    M._tw_create(sampleRate, windowlen, 128);
    M._tw_silent();

    for (const [name, b] of sounds ?? [])
        apply(M, { type: 'sample', name, bytes: b }, {});

    if (sound)
        apply(M, { type: 'sample', name: sound.name, bytes: sound.bytes }, {});

    return M;
}

const wavOf = (sound) => (sound ? `/dsp/${sound.name}` : '');

/* A generation scored by `ears': each source to whichever listener is
 * free, the distances back in the sources' order (thinkweb.cpp,
 * twScoreAll). One that cannot hear fails every generation from then
 * on. */
function scorer (ears)
{
    const free = [...ears];
    const queue = [];
    const pending = new Map();      /* id -> { resolve, reject }          */
    let next = 0;
    let deaf = null;

    const pump = () =>
    {
        while (free.length > 0 && queue.length > 0)
        {
            const { id, source, resolve, reject } = queue.shift();
            const ear = free.pop();

            pending.set(id, { resolve, reject });
            ear.postMessage({ id, source });
        }
    };

    for (const ear of ears)
        ear.onmessage = (e) =>
        {
            if (e.data.why !== undefined)
            {
                deaf = new Error(`a listener could not hear: ${e.data.why}`);

                for (const { reject } of pending.values())
                    reject(deaf);

                pending.clear();
                return;
            }

            pending.get(e.data.id)?.resolve(e.data.distance);
            pending.delete(e.data.id);
            free.push(ear);
            pump();
        };

    return async (joined) =>
    {
        if (deaf !== null)
            throw deaf;

        const scores = joined.split('\x1e').map((source) =>
            new Promise((resolve, reject) =>
                queue.push({ id: next++, source, resolve, reject })));

        pump();

        return Promise.all(scores);
    };
}

async function search (m)
{
    const { spec, seed, sound, ears } = m;
    const shared = (ears ?? []).length > 0;
    const M = await start(shared ? './thinkevolve.js' : './thinkweb.js', m,
        (why) => postMessage({ type: 'done', ok: false, why }));
    const began = performance.now();

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

    if (shared)
        M.scoreAll = scorer(ears);

    const r = JSON.parse(await M.ccall('tw_evolve', 'string',
        ['string', 'string', 'string', 'string', 'string', 'number',
         'number', 'number', 'number'],
        [spec.start, spec.startArgs, spec.target, spec.targetArgs,
         wavOf(sound), spec.grow ? 1 : 0, seed, spec.generations ?? 0,
         shared ? 1 : 0],
        shared ? { async: true } : undefined));

    postMessage({ type: 'done', ...r });
}

/* A listener: the search's target heard once, and then each graph the
   search sends on `port', answered with how far it is. */
async function listen (m)
{
    const { spec, sound, port } = m;
    const M = await start('./thinkweb.js', m,
                          (why) => port.postMessage({ why }));
    const why = M.ccall('tw_evolve_listen', 'string',
        ['string', 'string', 'string', 'string', 'string'],
        [spec.start, spec.startArgs, spec.target, spec.targetArgs,
         wavOf(sound)]);

    if (why !== '')
    {
        port.postMessage({ why });
        return;
    }

    port.onmessage = (e) => port.postMessage({
        id: e.data.id,
        distance: M.ccall('tw_evolve_score', 'number', ['string'],
                          [e.data.source]),
    });
}

onmessage = (e) =>
{
    if (e.data.port)
        listen(e.data).catch((err) =>
            e.data.port.postMessage({ why: String(err) }));
    else
        search(e.data).catch((err) =>
            postMessage({ type: 'done', ok: false, why: String(err) }));
};
