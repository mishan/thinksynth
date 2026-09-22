#!/usr/bin/env node
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
 * miccheck.mjs -- a live input through the browser build, from Node.
 *
 *   node wasm/web/miccheck.mjs [BUILD_DIR]
 *
 * The module's side of the microphone. scripts/dspcapture holds the engine and
 * the accumulator to their promises natively; this holds the same promises
 * through tw_capture and the browser build, and adds the two that only exist
 * here: that the shipped graph loads in a module with its plugins linked in
 * rather than dlopened, and that a piece reaches a live input with one clause.
 *
 * What it checks, and each of these is something that would otherwise be found
 * by a person with headphones on:
 *
 *   silence is the default.       A piece carrying fx/vocoder-mic.dsp with
 *      nothing captured is silent, because a vocoder with no modulator has no
 *      band envelopes and lets none of the carrier through. That is also what
 *      makes such a piece render the same under genwav as it did before live
 *      input existed.
 *
 *   the capture is heard.         The same piece with a capture fed is not
 *      silent, and follows the capture: fed nothing, then something, the
 *      output arrives after the capture does and not before.
 *
 *   it is the capture.            A pass-through live effect hands back the
 *      frames that went in, which is the claim tw_capture makes and the only
 *      one a render can be held to sample for sample -- and it hands them back
 *      two windows later, because the worklet's quantum is 128 and the page
 *      runs a window of 256. scripts/dspcapture says where the two come from
 *      and why a page that wants a live input should ask for 128.
 *
 *   nothing else moves.          A graph that declares no live0 renders bit for
 *      bit the same while a capture is being fed, which is what lets a page
 *      keep the microphone open across every patch it offers.
 *
 * What this cannot see is the browser: getUserMedia, the worklet's input, and
 * whether a real audio thread delivers the two in the same call.
 * browsertest.mjs holds that against this, to the bit, with Chromium's
 * fake-device flags standing in for a microphone.
 *
 * Exit status is the number of failures.
 */

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

import { loadPiece, renderDirect, feedCapture } from './render.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const build = path.resolve(process.argv[2] ??
                           path.join(here, '..', '..', 'build-web'));

const { default: createThinkWeb } =
    await import(pathToFileURL(path.join(build, 'thinkweb.js')).href);

export const RATE = 48000;
export const WINDOW = 256;
export const BLOCK = 128;

let failures = 0;

const ok = (what) => process.stdout.write(`ok    ${what}\n`);

const fail = (what, detail) =>
{
    process.stdout.write(`FAIL  ${what}${detail ? ` -- ${detail}` : ''}\n`);
    failures++;
};

const okOrFail = (good, what, detail) => (good ? ok(what) : fail(what, detail));

const peak = (v, from = 0, to = v.length) =>
{
    let top = 0;

    for (let i = from; i < to && i < v.length; i++)
        if (Math.abs(v[i]) > top)
            top = Math.abs(v[i]);

    return top;
};

/* ---- what is fed --------------------------------------------------------- */

/* A capture with no two frames alike and no period, so a run that repeated or
   reordered a window could not come out looking right. The same fixed LCG
   scripts/dspcapture uses, and for the same reason: what is compared is two
   runs against each other, and Math.random is one more thing that could
   differ between them. Well inside the rails, so nothing is limited. */
export function makeCapture (frames)
{
    const out = new Float32Array(frames);

    let state = 12345n;

    for (let i = 0; i < frames; i++)
    {
        state = (state * 1103515245n + 12345n) & 0xffffffffn;

        out[i] = Number((state >> 16n) & 0xffffn) / 65535 * 0.4 - 0.2;
    }

    return out;
}

/* And a speech-shaped one, at the level a microphone actually delivers.
 *
 * THIS IS THE GATE THAT WAS MISSING. The vocoder shipped once with no gain on
 * its live input and could not be heard: a laptop microphone with its automatic
 * gain control switched off -- which wasm/web/mic.js switches off on purpose --
 * sits around 0.03 to 0.15 peak on ordinary speech, where the synth channel
 * fx/vocoder.dsp is usually driven by is around 0.5. The vocoded output is
 * linear in that, so a voice came out twenty to thirty dB under where the bands
 * were tuned to open. Every check passed, because every check fed it a signal
 * as loud as a synth.
 *
 * MIC_PEAK is deliberately at the quiet end of plausible, and the assertion
 * below is about how much the piece moved rather than whether it moved.
 *
 * Syllables and two wandering formants rather than the flat LCG above: a
 * vocoder's whole business is bands moving against each other, and a signal
 * with a fixed spectrum cannot tell that working from a gain.
 */
export const MIC_PEAK = 0.08;

export function makeSpeech (frames, peak = MIC_PEAK)
{
    const out = new Float32Array(frames);

    let state = 7n;
    let top = 0;

    for (let i = 0; i < frames; i++)
    {
        const t = i / RATE;

        state = (state * 1103515245n + 12345n) & 0xffffffffn;

        const noise = Number((state >> 16n) & 0xffffn) / 65535 * 2 - 1;
        const env = 0.5 + 0.5 * Math.sin(2 * Math.PI * 3 * t);
        const f1 = 500 + 300 * Math.sin(2 * Math.PI * 1.3 * t);
        const f2 = 1400 + 700 * Math.sin(2 * Math.PI * 0.9 * t);

        out[i] = env * (0.50 * Math.sin(2 * Math.PI * 120 * t) +
                        0.35 * Math.sin(2 * Math.PI * f1 * t) +
                        0.25 * Math.sin(2 * Math.PI * f2 * t) +
                        0.12 * noise);

        top = Math.max(top, Math.abs(out[i]));
    }

    for (let i = 0; i < frames; i++)
        out[i] *= peak / top;

    return out;
}

/* ---- the graphs ---------------------------------------------------------- */

/* Read off disk rather than written here: the point of the two piece checks is
   that the *shipped* graph works, and a copy in this file would be a check
   that this file's copy works. */
const vocoderMic =
    fs.readFileSync(path.join(build, 'dsp', 'fx', 'vocoder-mic.dsp'), 'utf8');

/* A carrier with a continuous spectrum, which is what a vocoder wants. Written
   here because it has to be uninteresting: a shipped patch would bring its own
   envelope and its own character into a measurement about the modulator. */
const CARRIER = `
name "miccheck-carrier";

node ionode {
    channels = 2;
    out0 = saw->out;
    out1 = saw->out;
};

node saw osc::simple {
    freq = ionode->freq;
    waveform = 1;
    amp = 0.5;
};

io ionode;
`;

/* And the pass-through: whatever comes out is what went in. A master effect,
   because live<N> is a channel effect's io node and there is no other way into
   one -- tw_load loads an instrument, and a voice graph has no live<N>. Its
   outputs replace the mix, so the carrier under it is discarded and what comes
   back is the capture alone. */
export const PASSTHROUGH = `
name "miccheck-live";

node ionode {
    channels = 2;
    in0 = 0;
    in1 = 0;
    live0 = 0;
    out0 = ionode->live0;
    out1 = ionode->live0;
};

io ionode;
`;

/* Everything a piece here may name, in one place: a page and a harness have to
   be handed the same files under the same names or they are not running the
   same piece. browsertest.mjs imports this. */
export const DSPS = {
    'miccheck-carrier.dsp': CARRIER,
    'miccheck-live.dsp': PASSTHROUGH,
    'fx/vocoder-mic.dsp': vocoderMic,
    'strings.dsp': fs.readFileSync(path.join(build, 'dsp', 'strings.dsp'),
                                   'utf8'),
    'fx/limiter.dsp': fs.readFileSync(path.join(build, 'dsp', 'fx',
                                                'limiter.dsp'), 'utf8'),
};

/* And the shipped piece, which is where the gain staging above actually has to
   work. Off disk for vocoderMic's reason. */
export const voiceGen =
    fs.readFileSync(path.join(build, 'gen', 'voice.gen'), 'utf8');

/* One held note on one instrument, and a master effect on the mix. Short,
   seeded, and with no generator in it: what is being measured is the effect,
   and a composer choosing notes would put its own envelope in the way. */
export const piece = (effect) => `
name "miccheck";
description "A held chord and one master effect.";
seed 1;

instrument pad {
    dsp "miccheck-carrier.dsp";
};

effect "${effect}";

chain held {
    stage src gen::eno_line {
        notes = "C3 E3 G3";
        period = 0.2 s; jitter = 0 s; prob = 1; hold = 4 s; vel = 100;
    };
    sink { instrument = pad; };
};
`;

/* A piece rendered for `frames', with `capture' fed a block at a time the way
   the worklet feeds a quantum. The transport is started at the next window,
   which is where the page's Play lands. */
export async function renderPiece ({ gen, capture, frames })
{
    const { M, ok: loaded, errors } = await loadPiece(createThinkWeb, {
        rate: RATE, windowlen: WINDOW, block: BLOCK, gen,
        instruments: DSPS,
    });

    if (!loaded)
        return { ok: false, errors, out: new Float32Array(0) };

    M._tw_transport(-1, 0, 0);

    const out = new Float32Array(frames);

    for (let done = 0; done < frames; done += BLOCK)
    {
        const n = Math.min(BLOCK, frames - done);

        if (capture !== null)
            feedCapture(M, capture, done, n);

        const p = M._tw_render(n) >> 2;

        /* The left side, frame by frame: the module hands back interleaved
           stereo and a vocoder is mono, so one side is the whole signal. */
        for (let i = 0; i < n; i++)
            out[done + i] = M.HEAPF32[p + i * 2];
    }

    return { ok: true, errors: [], out,
             dropped: M._tw_capture_dropped(),
             starved: M._tw_capture_starved() };
}

/* ---- the run ------------------------------------------------------------ */

async function main ()
{
    /* ---- the shipped graph loads at all ------------------------------------- */

    {
        const frames = RATE / 2;
        const quiet = await renderPiece({ gen: piece('fx/vocoder-mic.dsp'),
                                          capture: null, frames });

        okOrFail(quiet.ok,
                 'a piece carrying fx/vocoder-mic.dsp loads in the browser module',
                 quiet.errors.join('; '));

        /* ---- and with nothing captured it is silent ---------------------- */

        if (quiet.ok)
            okOrFail(peak(quiet.out) === 0,
                     'a live vocoder with nothing captured is silence, so a piece '
                     + 'carrying one still renders offline',
                     `it peaked at ${peak(quiet.out)}`);
    }

    /* ---- the capture is heard, and only after it arrives -------------------- */

    /* Fed silence for the first half and signal for the second. The vocoder's
     * output has to be exactly zero through the first half -- no envelopes, no
     * carrier -- and something in the second. Which is two claims in one render:
     * that it hears the capture, and that it does not hear it early.
     */
    {
        const frames = RATE;              /* one second */
        const half = frames / 2;
        const capture = makeCapture(frames);

        capture.fill(0, 0, half);

        const got = await renderPiece({ gen: piece('fx/vocoder-mic.dsp'),
                                        capture, frames });

        if (!got.ok)
            fail('the live vocoder run loads', got.errors.join('; '));
        else
        {
            /* A window of slack at the boundary: the capture for a window is fed
               over several quanta and heard one window later, so the frame it
               starts being audible at is not the frame it started being fed at.
               The claim is about halves of a second, not about that boundary. */
            const before = peak(got.out, 0, half);
            const after = peak(got.out, half + 4 * WINDOW, frames);

            okOrFail(before === 0 && after > 0,
                     'a live vocoder is driven by what was captured, and hears '
                     + 'none of it before it arrives',
                     `silent half ${before}, captured half ${after}`);

            okOrFail(got.dropped === 0 && got.starved === 0,
                     'the capture accumulator drops nothing and starves for '
                     + 'nothing at a quantum of 128 in a window of 256',
                     `dropped ${got.dropped}, starved ${got.starved}`);
        }
    }

    /* ---- the shipped piece is audibly driven by a real microphone ---------- */

    /* The check the first version of this file did not have, and the reason the
     * vocoder shipped unhearable: everything else here proves the capture
     * *arrives*, and nothing proved it arrived loud enough to do anything.
     *
     * gen/voice.gen at a 0.08-peak capture, against the same piece with none.
     * It carries `dry', so it makes a sound either way -- which makes this an
     * assertion about how much the vocoder added, and only the vocoder can have
     * added it. 1.3x is about 2.3 dB; the version that shipped managed 1.02x.
     */
    {
        const frames = RATE * 8;
        const speech = makeSpeech(frames);

        const quiet = await renderPiece({ gen: voiceGen, capture: null,
                                          frames });
        const loud = await renderPiece({ gen: voiceGen, capture: speech,
                                         frames });

        if (!quiet.ok || !loud.ok)
            fail('gen/voice.gen loads',
                 (quiet.ok ? loud.errors : quiet.errors).join('; '));
        else
        {
            const off = peak(quiet.out);
            const on = peak(loud.out);
            const ratio = off > 0 ? on / off : 0;

            okOrFail(off > 0 && ratio > 1.3,
                     'gen/voice.gen is audibly driven by a capture at the '
                     + `level a microphone actually gives (${MIC_PEAK} peak)`,
                     `${off.toFixed(3)} on its own, ${on.toFixed(3)} with it `
                     + `-- ${ratio.toFixed(2)}x, wanted over 1.3x`);
        }
    }

    /* ---- and it is the capture, sample for sample -------------------------- */

    /* The one check a render can be held to exactly. A master effect that is
     * nothing but `out0 = live0' replaces the mix with the capture, so what comes
     * out is what was fed -- delayed, and scaled by the master gain, which is one
     * ratio for the whole run. Comparing against that ratio rather than against an
     * absolute number is what keeps this a check on the samples rather than on the
     * output stage.
     *
     * The delay is two windows and not one, because a quantum of 128 is smaller
     * than a window of 256: the first ask for a window of capture comes with a
     * quantum in hand. scripts/dspcapture pins both numbers and gthSynthSource's
     * header says where they come from. It is asserted here rather than allowed for
     * because it is the number a page picks its window by.
     */
    {
        const frames = RATE / 4;
        const capture = makeCapture(frames);

        const got = await renderPiece({ gen: piece('miccheck-live.dsp'),
                                        capture, frames });

        if (!got.ok)
            fail('a pass-through live effect loads', got.errors.join('; '));
        else
        {
            const left = got.out;

            let lead = 0;

            while (lead < left.length && left[lead] === 0)
                lead++;

            const scale = lead < left.length ? left[lead] / capture[0] : 0;
            let worst = 0;
            let at = -1;

            for (let i = 0; lead + i < left.length; i++)
            {
                const off = Math.abs(left[lead + i] - capture[i] * scale);

                if (off > worst)
                {
                    worst = off;
                    at = i;
                }
            }

            okOrFail(lead > 0 && lead < left.length && scale !== 0 &&
                     worst < 1e-6,
                     'a live graph is handed the frames that were captured, in '
                     + 'order, through the browser module',
                     `it arrived ${lead} frames in; worst frame ${at} off by `
                     + `${worst}`);

            okOrFail(lead === 2 * WINDOW,
                     'two windows later, which is what a quantum smaller than the '
                     + 'window costs -- and the reason to ask for a window of 128',
                     `it arrived ${lead} frames in, not ${2 * WINDOW}`);
        }
    }

    /* ---- a graph asking for nothing renders identically -------------------- */

    /* The property the rest of the tree rests on. An ordinary voice graph -- which
     * is every shipped .dsp -- has to render bit for bit the same whether or not
     * the page has a microphone open, so that keeping one open across a patch
     * change is free and so that nothing in the corpus changed when live<N> arrived.
     */
    {
        const frames = RATE / 4;
        const capture = makeCapture(frames);
        const events = [{ on: true, frame: 0, note: 60, velocity: 100 }];

        const dry = await renderDirect(createThinkWeb, {
            rate: RATE, windowlen: WINDOW, block: BLOCK,
            text: CARRIER, capture: null, frames, events,
        });

        const wet = await renderDirect(createThinkWeb, {
            rate: RATE, windowlen: WINDOW, block: BLOCK,
            text: CARRIER, capture, frames, events,
        });

        if (!dry.ok || !wet.ok)
            fail('the two dry runs load',
                 dry.log.concat(wet.log).join('; '));
        else
        {
            let at = -1;

            for (let i = 0; i < dry.out.length; i++)
                if (dry.out[i] !== wet.out[i])
                {
                    at = i;
                    break;
                }

            okOrFail(at < 0 && peak(dry.out) > 0,
                     'a graph that asks for no live input renders bit for bit the '
                     + 'same while the module is being fed a capture',
                     at < 0 ? 'it rendered silence'
                            : `they differ at sample ${at}`);
        }
    }

    process.stdout.write(`\n${failures} failure(s)\n`);

    return failures;
}

if (import.meta.url === pathToFileURL(process.argv[1]).href)
    process.exit(await main());
