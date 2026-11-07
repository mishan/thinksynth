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
 * silent.js -- the module again, in a worker, composing what the worklet
 * composes and rendering nothing: what the mirror (mirror.js) and
 * the SDK's look-ahead (lookahead.js) are each built on.
 *
 * host.js runs either one in the same place and talks to both the same
 * way, so what such an instance owes the worklet is here once: the
 * growth the worklet's scheduler and the page ask for, the ear's answers
 * to this instance's own scheduler, and one frame counter with the
 * worklet.
 */

import createThinkWeb from './thinkweb.js';

export async function createSilent ({ bytes, windowlen, sampleRate }, log)
{
    const M = await createThinkWeb({
        instantiateWasm: (imports, done) =>
        {
            WebAssembly.instantiate(bytes, imports)
                .then((r) => done(r.instance, r.module))
                .catch((e) => log(`the wasm did not compile: ${e}`));

            return {};
        },
        print: log,
        printErr: log,
    });

    M._tw_create(sampleRate, windowlen, 128);

    /* A worker can take a growth step's tenth of a second: this instance
       grows its own, and the worklet's too (answer below). */
    M._tw_grow_mode(1);

    /* And hears through the page's ear worker, as the worklet does: a
       render here would hold up the worker's own work. A gen::graft
       generation, seconds of renders, is bred there too. */
    M._tw_ear_mode(2);
    M._tw_graft_mode(2);

    /* Before anything is loaded: it is a kind of synth and not a mode a
       running one flips (thSynth::setSilent). */
    M._tw_silent();

    return M;
}

/* The worklet's side of things, if `m' is one of them. */
export function answer (M, m)
{
    switch (m.type)
    {
        /* A growth step the worklet's scheduler asked for, grown here and
           sent back through the page (host.js). */
        case 'growask':
        {
            const a = JSON.parse(m.ask);
            const r = JSON.parse(M.ccall('tw_grow', 'string',
                                         ['string', 'string', 'number',
                                          'string', 'number'],
                                         [a.text, a.base, a.choice,
                                          a.kinds, a.want]));

            postMessage({ type: 'grown', ticket: a.ticket, ...r });
            return true;
        }

        /* One for the page itself: an edit of a patch, asked for by hand
           (growui.js). */
        case 'grownow':
        {
            let r;

            /* Answered whatever happens: the page waits on it. */
            try
            {
                r = JSON.parse(M.ccall('tw_grow', 'string',
                    ['string', 'string', 'number', 'string', 'number'],
                    [m.text, m.base, m.choice, m.kinds, m.want]));
            }
            catch (e)
            {
                r = { grown: '', what: '', why: String(e) };
            }

            postMessage({ type: 'grownnow', id: m.id, ...r });
            return true;
        }

        /* What the ear worker bred for this instance's scheduler. */
        case 'bred':
            M.ccall('tw_graft_answer', null,
                    ['number', 'number', 'string', 'string', 'number',
                     'string'],
                    [m.ticket, m.ok ? 1 : 0, m.flock, m.champion, m.depth,
                     m.why]);
            return true;

        /* What the ear worker heard for this instance's scheduler. */
        case 'heard':
            M.ccall('tw_ear_answer', null,
                    ['number', 'number', 'number', 'string'],
                    [m.ticket, m.ok ? 1 : 0, m.distance, m.tuned]);
            return true;

        /* This instance's frames are the worklet's from here on
           (thinkweb.cpp, tw_align). */
        case 'align':
            M._tw_align(m.frame);
            return true;
    }

    return false;
}

/* What this instance's scheduler asked to hear, or to breed, after each
   message. */
export function askToHear (M)
{
    for (let ask; M !== null && (ask = M.UTF8ToString(M._tw_ear_next())); )
        postMessage({ type: 'hear', ask });

    for (let ask; M !== null && (ask = M.UTF8ToString(M._tw_graft_next())); )
        postMessage({ type: 'graft', ask });
}
