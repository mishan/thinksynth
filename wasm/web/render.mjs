/*
 * Copyright (C) 2004-2026 Metaphonic Labs
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
 * render.mjs -- the browser build's module, called directly rather than
 * through a worklet.
 *
 * The same calls worklet.js makes, in the same order, a block at a time:
 * which is what lets browsertest.mjs hold a browser's render against this
 * one to the bit, and what check.mjs and piececheck.mjs use to play every
 * shipped patch and every shipped piece without a browser at all.
 */

import { drain, loadErrors, tapeLine } from '../tape.mjs';

/* A fresh module per render: a fresh synth, and a fresh plugin load, which
   is what restarts osc::static's noise. */
export async function renderDirect (createThinkWeb,
                                    { rate = 48000, windowlen = 256,
                                      block = 128, text, events = [],
                                      frames })
{
    const log = [];
    const M = await createThinkWeb({
        print: (s) => log.push(s),
        printErr: (s) => log.push(s),
    });

    const took = M._tw_create(rate, windowlen, block);
    const ok = M.ccall('tw_load', 'number', ['number', 'string'],
                       [0, text]) !== 0;

    for (const e of events)
    {
        if (e.on)
            M._tw_note_on(e.frame, 0, e.note, e.velocity);
        else
            M._tw_note_off(e.frame, 0, e.note);
    }

    const out = new Float32Array(frames * 2);

    for (let done = 0; done < frames; done += block)
    {
        const n = Math.min(block, frames - done);
        const p = M._tw_render(n) >> 2;

        out.set(M.HEAPF32.subarray(p, p + n * 2), done * 2);
    }

    return { ok, log, out, windowlen: took };
}

/* A fresh module with a piece loaded into it, the way the page does it:
 * the synth made at the rate and window asked for, the .dsp files a piece
 * may name handed over first, then the .gen. `ok' says whether it parsed
 * and `errors' are the loader's own words when it did not.
 *
 * `instruments' is the .dsp files by name; a piece's instruments are
 * looked up among them, and the module has no other way to reach a file.
 *
 * One function, because every harness here and the bench start this way
 * and a load that gained a step -- a seed to agree on, say -- would
 * otherwise have to gain it in each. */
export async function loadPiece (createThinkWeb,
                                 { rate = 48000, windowlen = 256,
                                   block = 128, gen, instruments = {},
                                   seed = -1 })
{
    const log = [];
    const M = await createThinkWeb({
        print: (s) => log.push(s),
        printErr: (s) => log.push(s),
    });

    const windowTaken = M._tw_create(rate, windowlen, block);

    for (const [name, text] of Object.entries(instruments))
        M.ccall('tw_instrument', 'number', ['string', 'string'],
                [name, text]);

    const ok = M.ccall('tw_piece_load', 'number', ['string', 'number'],
                       [gen, seed]) !== 0;

    return { M, ok, log, errors: ok ? [] : loadErrors(M),
             windowlen: windowTaken };
}

/* A .gen, composed for `seconds' of transport, and the tape it delivered.
 *
 * The worklet's loop with the audio taken out: the same module, the same
 * per-block calls, the transport stepped once per window inside tw_render
 * exactly as it is under a real audio thread. What comes back is the tape
 * as text, written the way genwav writes it, which is the thing M2's gate
 * compares (JAM.md, section 6). */
export async function playPiece (createThinkWeb,
                                 { rate = 48000, windowlen = 256,
                                   block = 128, gen, instruments = {},
                                   seconds = 60, commands = [] })
{
    const { M, ok, log, errors, windowlen: took } =
        await loadPiece(createThinkWeb,
                        { rate, windowlen, block, gen, instruments });

    if (!ok)
        return { ok, log, errors, tape: '' };

    /* Stamped at -1: the next window, which is where the page's Play lands
       too. */
    M._tw_transport(-1, 0, 0);

    /* The scheduler's commands, each with the transport time it applies
       at: { at, op: 'knob', knob, value }, { at, op: 'tempo', value } or
       { at, op: 'stop' }. Handed over up front; the module holds each
       until its time and applies it inside the step there. */
    for (const c of commands)
        schedule(M, c);

    let tape = '';

    while (M._tw_now() < seconds)
    {
        M._tw_render(block);

        for (const e of drain(M))
            tape += tapeLine(e);
    }

    return { ok: true, log, errors: [], tape, now: M._tw_now(),
             windowlen: took };
}

/* thinkweb.cpp's TransportOp, as far as tw_at takes it: the two ops that
   carry a transport time. worklet.js has the whole table, start and
   rewind included, because it is what the page posts through. */
const AT_OP = { stop: 1, tempo: 3 };

/* One scheduler command into the module, as the worklet would post it.
 *
 * Every host here goes through this rather than spelling the op numbers
 * again: the harnesses compare a browser's tape against this path's, and
 * two spellings of the enum is how a renumbering turns into a gate that
 * passes while comparing different commands. */
export function schedule (M, c)
{
    if (c.op === 'knob')
        M._tw_knob(c.at, c.knob, c.value);
    else if (Object.hasOwn(AT_OP, c.op))
        M._tw_at(c.at, AT_OP[c.op], c.value ?? 0);
    else
        throw new Error(`no scheduler command '${c.op}'`);
}

/* A piece played the way the page plays it: loaded, then aimed, then
 * listened to.
 *
 * The page's rule, in Node (AIMING.md, section 3): what a channel sounds
 * like is the piece's to decide, and where the piece is silent on it, the
 * page's defaults'. So the piece goes in first, the module says which
 * channels its sinks named and its own instruments did not take, and
 * `patchFor' is asked what belongs on each -- the same question patch.js
 * asks for the page, with the same answer, resolved by the caller because
 * only the caller can read a file.
 *
 * `patchFor(channel)' returns `{ name, dsp, args }': the .dsp's text and
 * the chanarg overrides to set on it afterwards, which is a .patch. null
 * for a channel it has nothing for.
 *
 * A piece fed by `input midi' composes nothing until somebody plays it,
 * so `chord' is held down on every channel it listens on -- there is no
 * peak to have otherwise, and hands.gen is the piece that is nothing but
 * input.
 *
 * Returns as soon as the peak passes `floor', because by then the
 * question is answered and the rest of the minute is time spent. Not at
 * the first sample above zero: a DC offset is above zero, and so is the
 * tail of a denormal, and neither is a piece being heard. -60 dBFS is
 * quiet enough to be nothing a person would call a sound and loud enough
 * that no shipped piece takes an extra window to reach it -- every one of
 * the seventeen crosses it in the same window it first leaves zero.
 */
export async function playAimed (createThinkWeb,
                                 { rate = 48000, windowlen = 256,
                                   block = 128, gen, instruments = {},
                                   patchFor = () => null,
                                   chord = [53, 56, 60], seconds = 60,
                                   floor = 0.001 })
{
    const { M, ok, log, errors } =
        await loadPiece(createThinkWeb,
                        { rate, windowlen, block, gen, instruments });

    if (!ok)
        return { ok, log, errors, aimed: [], listens: [], peak: 0, at: 0 };

    const aimed = [];

    for (let i = 0; i < M._tw_sink_count(); i++)
    {
        const channel = M._tw_sink_channel(i);
        const what = patchFor(channel);

        if (what === null || what.dsp === undefined)
            continue;

        M.ccall('tw_load', 'number', ['number', 'string'],
                [channel, what.dsp]);

        /* After the load, as gthPatchManager::parse does it: the
           overrides are for the tree that load just built. */
        for (const a of what.args ?? [])
            M.ccall('tw_chanarg', 'number',
                    ['number', 'string', 'array', 'number'],
                    [channel, a.name,
                     new Uint8Array(Float32Array.from(a.values).buffer),
                     a.values.length]);

        aimed.push({ channel, patch: what.name });
    }

    const listens = [];

    for (let c = 0; c < 16; c++)
        if (M._tw_listens(c))
            listens.push(c);

    M._tw_transport(-1, 0, 0);

    let held = false;
    let peak = 0;

    while (M._tw_now() < seconds)
    {
        /* After a second of transport, which is where checkKeys puts its
           chord: a press at zero would land before the first step. */
        if (!held && M._tw_now() >= 1)
        {
            held = true;

            for (const c of listens)
                for (const note of chord)
                    M._tw_midi_on(-1, c, note, 100);
        }

        const p = M._tw_render(block) >> 2;

        for (let i = 0; i < block * 2; i++)
            peak = Math.max(peak, Math.abs(M.HEAPF32[p + i]));

        /* Drained, or the module holds a minute of a busy piece's events
           for nobody. */
        drain(M);

        if (peak > floor)
            break;
    }

    return { ok: true, log, errors: [], aimed, listens, peak,
             at: M._tw_now() };
}

/* A piece played *at*: keys held down and let go, into whatever chains
 * declared `input midi' on the channel they arrive on.
 *
 * The other half of M2's command path. A composed note declares how long it
 * lasts and the scheduler derives its release; a key held down has no idea
 * how long it will be held, so it goes in with a duration of zero and the
 * release is its own event -- which is why the composer ABI has a
 * THC_EV_NOTEOFF at all. An arpeggiator is the plugin that forced it, and
 * gen/hands.gen is the piece built on it.
 *
 * `patches' puts a .dsp on a channel first, the way the desktop's Patch
 * Selector does: a piece that routes input to a channel it declares no
 * instrument for has nothing there to sound. The channels are the engine's
 * -- a .gen file writes `channel = 1' for the first one and the loader
 * hands over 0.
 *
 * `keys' is a script in transport seconds: { at, channel, note, velocity }
 * for a press, velocity omitted for a release.
 */
export async function playAt (createThinkWeb,
                              { rate = 48000, windowlen = 256, block = 128,
                                gen, instruments = {}, patches = {},
                                keys = [], seconds = 10 })
{
    const { M, ok, log, errors } =
        await loadPiece(createThinkWeb,
                        { rate, windowlen, block, gen, instruments });

    if (!ok)
        return { ok, log, errors, events: [], peak: 0 };

    for (const [channel, text] of Object.entries(patches))
        M.ccall('tw_load', 'number', ['number', 'string'],
                [Number(channel), text]);

    M._tw_transport(-1, 0, 0);

    const script = [...keys].sort((a, b) => a.at - b.at);
    const events = [];
    let next = 0;
    let peak = 0;

    while (M._tw_now() < seconds)
    {
        /* Stamped at -1, the next window, because that is what a key
           pressed now is -- the script says when the finger moves, not
           which frame the engine should pretend it moved at. */
        for (; next < script.length && script[next].at <= M._tw_now(); next++)
        {
            const k = script[next];

            if (k.velocity === undefined)
                M._tw_midi_off(-1, k.channel, k.note);
            else
                M._tw_midi_on(-1, k.channel, k.note, k.velocity);
        }

        const p = M._tw_render(block) >> 2;

        for (let i = 0; i < block * 2; i++)
            peak = Math.max(peak, Math.abs(M.HEAPF32[p + i]));

        drain(M, events);
    }

    return { ok: true, log, errors: [], events, peak };
}
