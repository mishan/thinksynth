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
 * mictest.mjs -- a live input in real browsers.
 *
 *   cd wasm/web && npm ci && npx playwright install chromium firefox
 *   node mictest.mjs [BUILD_DIR]
 *
 * miccheck.mjs holds the module to its promises from Node; this holds the page
 * to the part Node has no way to reach -- the worklet node's input, what
 * `inputs[0]' is in a real audio thread's quantum, and getUserMedia.
 *
 * Its own harness rather than two more legs in browsertest.mjs because it needs
 * neither of the two things that file needs: no piece corpus, since what is
 * being measured is one effect, and no Node wasm build, since what the render
 * is held against is the browser build called directly. So this runs on a
 * checkout that has only built the site.
 *
 * TWO LEGS, and the split is the whole design.
 *
 *   exact, offline, both browsers.   An AudioBufferSourceNode of known samples
 *      connected to the worklet's *input*, rendered on an
 *      OfflineAudioContext, and held sample for sample against what the same
 *      capture gives through render.mjs under Node. No microphone is involved,
 *      which is what makes it exact and what makes it run anywhere: what it
 *      gates is the plumbing -- the node's input, the quantum, the sum to
 *      mono, tw_capture, live0.
 *
 *   present, live, through a device.  getUserMedia, createMediaStreamSource
 *      and a connect, on a live context, asking only whether audio reached the
 *      graph. It cannot be exact: a live context's quanta arrive when they
 *      arrive and a fake device's stream is aligned to nothing. But it is the
 *      only leg that touches the call a person's click makes, and a vocoder
 *      with no modulator is silence -- so anything at all coming out is the
 *      microphone.
 *
 *   and the shipped piece.          gen/voice.gen fetched off the site and
 *      played twice, once with a device and once without, asserting it got
 *      louder. That is the walk a person takes -- pick it out of the menu,
 *      press Play, press Live in -- and the only leg that touches the piece,
 *      its `dry' blend and the index the page fetches.
 *
 * Exit status is the number of browsers that failed.
 */

import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

import { chromium, firefox } from 'playwright';

import { DSPS, RATE, WINDOW, makeCapture, piece, renderPiece }
    from './miccheck.mjs';
import { serve } from './serve.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const build = path.resolve(process.argv[2] ??
                           path.join(here, '..', '..', 'build-web'));

/* A quarter of a second for the exact leg -- long enough to cross several
   windows and short enough that a page can hand the whole capture over as an
   array. */
const FRAMES = RATE / 4;

/* And two seconds of real time for the device leg, which is a wait rather than
   a render. */
const LIVE_SECONDS = 2;

/* ---- the wav Chromium reads as a microphone ---------------------------- */

/* Written rather than shipped, for scripts/makekit.sh's reason: a generated
 * file regenerates, changes when the thing that makes it changes, and carries
 * no provenance question. Into a temp directory, because the source tree is not
 * a scratch space and the build tree belongs to CMake.
 *
 * BROADBAND, and that matters. A vocoder measures sixteen bands and lets
 * through the same bands of the carrier; a sine would light one band and the
 * leg would be asserting that a sixteenth of the machine works. Harmonics plus
 * noise puts something in all of them.
 *
 * AND QUIET, WHICH MATTERS MORE, because getting this wrong is what shipped a
 * vocoder nobody could hear. The first version of this file wrote a signal at
 * 0.65 peak -- about what a synth channel puts out, and eight times what a
 * laptop microphone with its automatic gain control switched off gives on
 * ordinary speech. The leg passed; the thing did not work. A gate whose input
 * is louder than the real input is a gate that tests a case nobody is in.
 *
 * MIC_PEAK is therefore deliberately at the quiet end of plausible, so that a
 * gain staging which only works for a shouter fails here.
 */
const MIC_PEAK = 0.08;

function writeMicWav ()
{
    const rate = 48000;
    const frames = rate;               /* one second, looped by Chromium */
    const bytes = Buffer.alloc(44 + frames * 2);

    bytes.write('RIFF', 0, 'ascii');
    bytes.writeUInt32LE(36 + frames * 2, 4);
    bytes.write('WAVE', 8, 'ascii');
    bytes.write('fmt ', 12, 'ascii');
    bytes.writeUInt32LE(16, 16);
    bytes.writeUInt16LE(1, 20);        /* PCM */
    bytes.writeUInt16LE(1, 22);        /* mono */
    bytes.writeUInt32LE(rate, 24);
    bytes.writeUInt32LE(rate * 2, 28); /* bytes a second */
    bytes.writeUInt16LE(2, 32);        /* bytes a frame */
    bytes.writeUInt16LE(16, 34);
    bytes.write('data', 36, 'ascii');
    bytes.writeUInt32LE(frames * 2, 40);

    let state = 1;

    const raw = new Float32Array(frames);
    let top = 0;

    for (let i = 0; i < frames; i++)
    {
        const t = i / rate;

        state = (state * 1103515245 + 12345) & 0x7fffffff;

        const noise = (state / 0x7fffffff) * 2 - 1;

        /* Syllables, two wandering formants and breath: enough shape that the
           bands move against each other rather than all together, which is
           what tells a vocoder from a gain. */
        const env = 0.5 + 0.5 * Math.sin(2 * Math.PI * 3 * t);
        const f1 = 500 + 300 * Math.sin(2 * Math.PI * 1.3 * t);
        const f2 = 1400 + 700 * Math.sin(2 * Math.PI * 0.9 * t);

        raw[i] = env * (0.50 * Math.sin(2 * Math.PI * 120 * t) +
                        0.35 * Math.sin(2 * Math.PI * f1 * t) +
                        0.25 * Math.sin(2 * Math.PI * f2 * t) +
                        0.12 * noise);

        top = Math.max(top, Math.abs(raw[i]));
    }

    for (let i = 0; i < frames; i++)
    {
        const v = raw[i] * (MIC_PEAK / top);

        bytes.writeInt16LE(Math.max(-32768,
                                    Math.min(32767, Math.round(v * 32767))),
                           44 + i * 2);
    }

    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'thinkmic-'));
    const file = path.join(dir, 'micfake.wav');

    fs.writeFileSync(file, bytes);

    return file;
}

const MIC_WAV = writeMicWav();

/* What each browser needs to answer getUserMedia with no person and no device.
 *
 * Chromium takes a file, which is why its leg is worth more than Firefox's: a
 * known signal in means the assertion can be about a signal. Firefox has
 * `media.navigator.streams.fake', a tone with no way to choose it, so its leg
 * makes the same claim from a tone.
 */
function launchOptions (label)
{
    if (label === 'chromium')
        return { args: ['--use-fake-ui-for-media-stream',
                        '--use-fake-device-for-media-stream',
                        `--use-file-for-fake-audio-capture=${MIC_WAV}`,
                        '--autoplay-policy=no-user-gesture-required'] };

    return { firefoxUserPrefs: {
        'media.navigator.streams.fake': true,
        'media.navigator.permission.disabled': true,
        'permissions.default.microphone': 1,
        'media.autoplay.default': 0,
        'media.autoplay.blocking_policy': 0,
    } };
}

/* ---- the page's two runs ----------------------------------------------- */

/* A capture into the worklet's own input, offline and exact.
 *
 * An AudioBufferSourceNode rather than a microphone, which is what makes this
 * repeatable: the buffer is at the context's rate so nothing resamples it, and
 * start(0) puts its first frame on the context's first, so the worklet's first
 * quantum is the buffer's first 128 samples. Everything past that is the path a
 * microphone takes.
 *
 * Mono in, because the engine's capture is -- the sum would otherwise be the
 * thing under test rather than the delivery.
 */
async function captureInBrowser (page, gen, capture, frames, windowlen)
{
    return page.evaluate(async ({ gen, dsps, capture, frames, windowlen,
                                  rate }) =>
    {
        const { createSynth } = await import('./host.js');
        const ctx = new OfflineAudioContext({ numberOfChannels: 2,
                                              length: frames,
                                              sampleRate: rate });
        const logs = [];
        const synth = await createSynth(ctx, { windowlen,
                                               onLog: (s) => logs.push(s) });

        synth.node.connect(ctx.destination);

        for (const [name, text] of Object.entries(dsps))
            synth.instrument(name, text);

        const loaded = await synth.loadPiece(gen);

        if (loaded.errors.length > 0)
            return { ok: false, logs, errors: loaded.errors, left: [] };

        /* Onto the input host.js gives the node for a microphone -- and not to
           the destination, since what is being listened to is what the graph
           made of it rather than the capture itself. */
        const buf = ctx.createBuffer(1, capture.length, rate);

        buf.getChannelData(0).set(capture);

        const src = ctx.createBufferSource();

        src.buffer = buf;
        src.connect(synth.node);
        src.start(0);

        /* Started and acknowledged before the render: an OfflineAudioContext
           renders the lot in one go and would outrun a control message still in
           flight, which browsertest.mjs learned the hard way. */
        synth.transport('start');
        await synth.flush();

        const out = await ctx.startRendering();

        return { ok: true, logs, errors: [],
                 left: Array.from(out.getChannelData(0)) };
    }, { gen, dsps: DSPS, capture: Array.from(capture), frames, windowlen,
         rate: RATE });
}

/* And the microphone itself, opened the way a page opens it.
 *
 * An analyser after the worklet says what came out without anybody having to
 * listen, and it is after rather than before on purpose: what is being asked is
 * whether the *graph* heard the microphone, and a tap on the stream would
 * answer a different question.
 */
async function micInBrowser (page, gen, seconds, windowlen)
{
    return page.evaluate(async ({ gen, dsps, seconds, windowlen }) =>
    {
        const { createSynth } = await import('./host.js');
        const { openMic } = await import('./mic.js');

        const ctx = new AudioContext();
        const logs = [];
        const synth = await createSynth(ctx, { windowlen,
                                               onLog: (s) => logs.push(s) });

        for (const [name, text] of Object.entries(dsps))
            synth.instrument(name, text);

        const loaded = await synth.loadPiece(gen);

        if (loaded.errors.length > 0)
            return { opened: false, why: loaded.errors.join('; '), label: '',
                     warnings: [], peak: 0, logs };

        const tap = ctx.createAnalyser();

        tap.fftSize = 2048;
        synth.node.connect(tap);
        tap.connect(ctx.destination);

        await ctx.resume();

        let mic = null;
        let why = '';

        try
        {
            mic = await openMic(ctx, synth.node);
        }
        catch (e)
        {
            why = e.message;
        }

        synth.transport('start');
        await synth.flush();

        const samples = new Float32Array(tap.fftSize);
        let peak = 0;

        /* Real time, so this is a wait. Sampled repeatedly rather than once at
           the end: what is being looked for is whether audio ever arrived, and
           a single look could land in a gap. */
        for (let i = 0; i < seconds * 20; i++)
        {
            await new Promise((r) => setTimeout(r, 50));

            tap.getFloatTimeDomainData(samples);

            for (const v of samples)
                peak = Math.max(peak, Math.abs(v));
        }

        const out = { opened: mic !== null, why,
                      label: mic ? mic.label : '',
                      warnings: mic ? mic.warnings() : [],
                      peak, logs };

        if (mic)
            mic.close();

        await ctx.close();

        return out;
    }, { gen, dsps: DSPS, seconds, windowlen });
}

/* The shipped piece this is all for. Fetched by the page from the site the
   server below is serving, which is the path the Piece menu takes. */
const SHIPPED = 'voice.gen';

/* One of the shipped pieces, played the way the page plays it, with or without
 * a microphone. Live, because a device is, and measured through an analyser for
 * the same reason micInBrowser is.
 */
async function pieceInBrowser (page, name, withMic, seconds, windowlen)
{
    return page.evaluate(async ({ name, withMic, seconds, windowlen }) =>
    {
        const { createSynth } = await import('./host.js');
        const { openMic } = await import('./mic.js');

        const grab = async (p) =>
        {
            const r = await fetch(p);

            if (!r.ok)
                throw new Error(`${p}: ${r.status}`);

            return r.text();
        };

        /* Every .dsp the piece may name, which is what the page hands over
           before any piece is loaded: a worklet cannot fetch. */
        const index = JSON.parse(await grab('dsp/index.json'));
        const ctx = new AudioContext();
        const logs = [];
        const synth = await createSynth(ctx, { windowlen,
                                               onLog: (s) => logs.push(s) });

        for (const dsp of index.filter((n) => !n.startsWith('samples/')))
            synth.instrument(dsp, await grab(`dsp/${dsp}`));

        const loaded = await synth.loadPiece(await grab(`gen/${name}`));

        if (loaded.errors.length > 0)
            return { peak: 0, why: loaded.errors.join('; ') };

        const tap = ctx.createAnalyser();

        tap.fftSize = 2048;
        synth.node.connect(tap);
        tap.connect(ctx.destination);

        await ctx.resume();

        let mic = null;

        if (withMic)
        {
            try
            {
                mic = await openMic(ctx, synth.node);
            }
            catch (e)
            {
                return { peak: 0, why: e.message };
            }
        }

        synth.transport('start');
        await synth.flush();

        const samples = new Float32Array(tap.fftSize);
        let peak = 0;

        for (let i = 0; i < seconds * 20; i++)
        {
            await new Promise((r) => setTimeout(r, 50));

            tap.getFloatTimeDomainData(samples);

            for (const v of samples)
                peak = Math.max(peak, Math.abs(v));
        }

        if (mic)
            mic.close();

        await ctx.close();

        return { peak, why: '', logs };
    }, { name, withMic, seconds, windowlen });
}

/* ---- the run ----------------------------------------------------------- */

const server = await serve(build, 0);
const url = `http://127.0.0.1:${server.address().port}/`;

/* Rendered once, before either browser starts: it is what both are held
   against, and rendering it twice would be two chances to differ. */
const capture = makeCapture(FRAMES);
const exactGen = piece('miccheck-live.dsp');
const want = await renderPiece({ gen: exactGen, capture, frames: FRAMES });

if (!want.ok)
{
    process.stdout.write('mictest: the reference did not load -- ' +
                         `${want.errors.join('; ')}\n`);
    server.closeAllConnections();
    server.close();
    process.exit(1);
}

async function runBrowser (label, type)
{
    let browser;

    try
    {
        browser = await type.launch(launchOptions(label));
    }
    catch (e)
    {
        process.stdout.write(`FAIL  ${label}: could not launch -- ` +
                             `${e.message.split('\n')[0]}\n`);
        return false;
    }

    const page = await browser.newPage();
    const errors = [];

    page.on('pageerror', (e) => errors.push(e.message));
    await page.goto(url);

    let ok = true;

    /* ---- exact ------------------------------------------------------- */

    {
        let got;

        try
        {
            got = await captureInBrowser(page, exactGen, capture, FRAMES,
                                         WINDOW);
        }
        catch (e)
        {
            got = { ok: false, errors: [e.message.split('\n')[0]], left: [] };
        }

        if (!got.ok)
        {
            ok = false;
            process.stdout.write(`FAIL  ${label} live input: did not load ` +
                                 `-- ${got.errors.join('; ')}\n`);
        }
        else
        {
            let first = -1, count = 0, top = 0;

            for (let i = 0; i < FRAMES; i++)
            {
                top = Math.max(top, Math.abs(want.out[i]));

                /* fround because what comes back from a page is a double
                   holding a float, which is browsertest.mjs's reason too. */
                if (Math.fround(got.left[i]) !== want.out[i])
                {
                    if (first < 0)
                        first = i;
                    count++;
                }
            }

            if (top === 0 || count > 0)
            {
                ok = false;
                process.stdout.write(
                    `FAIL  ${label} live input: ` +
                    (top === 0 ? 'the reference is silent'
                     : `${count} samples differ, the first at frame ` +
                       `${first}`) + '\n');
            }
            else
                process.stdout.write(
                    `ok    ${label} live input: ${FRAMES} frames of capture ` +
                    `identical, peak ${top.toFixed(3)}\n`);
        }
    }

    /* ---- and through a device ---------------------------------------- */

    {
        let got;

        try
        {
            got = await micInBrowser(page, piece('fx/vocoder-mic.dsp'),
                                     LIVE_SECONDS, WINDOW);
        }
        catch (e)
        {
            got = { opened: false, why: e.message.split('\n')[0], peak: 0,
                    warnings: [], logs: [] };
        }

        if (!got.opened || got.peak === 0)
        {
            ok = false;
            process.stdout.write(
                `FAIL  ${label} microphone: ` +
                (got.opened ? 'it opened and the graph heard nothing'
                            : `it did not open -- ${got.why}`) + '\n');
        }
        else
            process.stdout.write(
                `ok    ${label} microphone: '${got.label}' reached the ` +
                `vocoder, peak ${got.peak.toFixed(3)}` +
                (got.warnings.length > 0
                    ? ` (${got.warnings.join('; ')})` : '') + '\n');
    }

    /* ---- and the shipped piece, the way a person will ----------------- */

    /* gen/voice.gen off the site rather than a piece written here: what is
     * being asked is whether the thing somebody picks out of the menu does
     * what its own header says it does. It carries `dry', so it is audible
     * before the microphone is on -- which makes the assertion "it got
     * louder" rather than "it made a sound", and that is the stronger one
     * anyway since only the vocoder can have added the difference.
     */
    {
        let quiet, loud;

        try
        {
            quiet = await pieceInBrowser(page, SHIPPED, false, LIVE_SECONDS,
                                         WINDOW);
            loud = await pieceInBrowser(page, SHIPPED, true, LIVE_SECONDS,
                                        WINDOW);
        }
        catch (e)
        {
            quiet = { peak: 0, why: e.message.split('\n')[0] };
            loud = { peak: 0, why: '' };
        }

        /* How much louder it has to get, and why the number is loose.
         *
         * The gain staging is pinned in miccheck.mjs, offline and exactly: the
         * same piece and the same 0.08-peak capture there gives 2.0x, against a
         * 1.3x threshold, and that is the regression guard for the vocoder that
         * shipped unhearable. This leg cannot be that. It runs in real time
         * against a device nobody here controls, reads the output through an
         * analyser that sees 2048 frames every 50 ms rather than all of them,
         * and starts measuring while the chord is still filling in -- so it
         * comes out at 1.38x to 1.44x over repeats where the offline number is
         * 2.0x. Holding a coarse measurement to a tight bound is how a gate
         * starts flaking.
         *
         * So 1.15x here: comfortably above the 1.05x the bug produced, and
         * comfortably below what three runs of this measured. What this leg is
         * for is the path -- getUserMedia to a graph -- and the number is the
         * loosest one that still fails the bug.
         *
         * Firefox gets 1.0x. Its fake device is a tone at a level it chooses,
         * and a single sine lights one of sixteen bands; there is nothing to
         * hold it to but connectedness. */
        const want = label === 'chromium' ? 1.15 : 1.0;
        const ratio = quiet.peak > 0 ? loud.peak / quiet.peak : 0;

        if (quiet.peak === 0 || ratio <= want)
        {
            ok = false;
            process.stdout.write(
                `FAIL  ${label} ${SHIPPED}: ` +
                (quiet.peak === 0
                    ? `it was silent without a microphone -- ${quiet.why}`
                    : `the microphone moved it by ${ratio.toFixed(2)}x, ` +
                      `wanted over ${want}x: ${quiet.peak.toFixed(3)} then ` +
                      `${loud.peak.toFixed(3)}`) + '\n');
        }
        else
            process.stdout.write(
                `ok    ${label} ${SHIPPED}: ${quiet.peak.toFixed(3)} on its ` +
                `own, ${loud.peak.toFixed(3)} with a ${MIC_PEAK} microphone ` +
                `(${ratio.toFixed(2)}x)\n`);
    }

    for (const e of errors)
    {
        process.stdout.write(`FAIL  ${label}: page error: ${e}\n`);
        ok = false;
    }

    await browser.close();

    return ok;
}

const failed = (await Promise.all(
    [['chromium', chromium], ['firefox', firefox]]
        .map(([label, type]) => runBrowser(label, type))))
    .filter((good) => !good).length;

/* close() alone waits for the browsers' keep-alive connections, which outlive
   the browsers here and keep the process up. */
server.closeAllConnections();
server.close();

process.stdout.write(`\n${failed === 0 ? 'all browsers passed'
                                       : `${failed} browser(s) failed`}\n`);
process.exitCode = failed;
