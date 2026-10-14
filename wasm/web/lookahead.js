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
 * lookahead.js -- the SDK's look-ahead: the mirror's shape (silent.js), a
 * bar or two in front of the worklet instead of level with it.
 *
 * host.js runs this in the mirror's place (`mirrorScript'), so it is fed
 * every message the worklet is fed and told each frame the worklet's tape
 * reached. It steps its own silent synth to the horizon that frame implies
 * (horizon.js) and posts what the scheduler delivered on the way: the
 * notes the worklet will play, before it plays them.
 *
 * Same seed, same commands at the same stamps, so the same notes.
 */

import { apply } from './engine.js';
import { answer, askToHear, createSilent } from './silent.js';
import { drain } from './tape.js';
import { horizonOf } from './horizon.js';

let M = null;
let rate = 0;
let windowlen = 0;
let barSeconds = 0;
let bars = 0;
const early = [];

const log = (text) => postMessage({ type: 'log',
                                    text: `lookahead: ${text}` });

async function start (m)
{
    M = await createSilent(m, log);
    rate = m.sampleRate;
    windowlen = m.windowlen;

    for (const e of early.splice(0))
        receive(e);
}

/* The piece's chains as its text says them: the instruments each plays,
   which is how a note's channel is turned back into its chain, and the
   stages in the document's order, which is how a stage is named in an
   edit (thinkweb.cpp, tw_gen_ops). */
function chains ()
{
    return JSON.parse(M.ccall('tw_piece_chains_json', 'string'))
        .map((c) => ({ name: c.name,
                       instruments: c.sinks.map((s) => s.instrument),
                       stages: c.stages.map((s) => s.name) }));
}

function step (frame)
{
    let target = frame;

    if (M._tw_running() && barSeconds > 0)
    {
        const origin = M._tw_origin();
        const horizon = horizonOf((frame - origin) / rate, barSeconds, bars);

        /* Short of the line by a window, so a command stamped on it is
           still ahead of this instance when it arrives. */
        target = Math.max(frame, origin + horizon * rate - windowlen);
    }

    if (target > M._tw_frame())
        M._tw_step(target);

    const events = drain(M).filter((e) => e.kind === 'N');

    postMessage({ type: 'ahead', epoch: M._tw_epoch(), events,
                  reached: M._tw_running() ? M._tw_now() : 0 });
}

function receive (m)
{
    if (m.type === 'start')
    {
        start(m).catch((e) => log(String(e)));
        return;
    }

    if (M === null)
    {
        early.push(m);
        return;
    }

    if (apply(M, m, { log,
                      piece: (id, ok) => ok && postMessage(
                          { type: 'chains', chains: chains(),
                            tempo: M._tw_tempo(),
                            meter: M._tw_meter() }) }) ||
        answer(M, m))
        return;

    switch (m.type)
    {
        case 'lookahead':
            barSeconds = m.barSeconds;
            bars = m.bars;
            break;

        case 'step':
            step(m.frame);
            break;
    }
}

onmessage = (e) =>
{
    receive(e.data);
    askToHear(M);
};
