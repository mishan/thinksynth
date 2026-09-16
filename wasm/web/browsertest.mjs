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
 * browsertest.mjs -- the worklet, in real browsers, held against what the
 * module does without one.
 *
 *   cd wasm/web && npm ci && npx playwright install chromium firefox
 *   node browsertest.mjs [BUILD_DIR]
 *
 * For each browser: serve the site and build the synth exactly as the page
 * does (host.js), but on an OfflineAudioContext. Then twice over.
 *
 * A patch, which is M1: play a phrase with stamped notes, render, and
 * compare every sample with what render.mjs gets from the same module
 * called from Node. It is one wasm file on both sides, so the two agree to
 * the bit or the plumbing between them is wrong -- the module handed to
 * the worklet, the messages, the stamps, the 128-frame quanta, the
 * de-interleave.
 *
 * A piece, which is M2: load every seeded .gen, run the transport for a
 * minute at a window of 256 and again at 128, and hold the tape that comes
 * back against the one genwav.mjs delivers under Node for the same seconds.
 * That is M2's gate (JAM.md, section 6), and it is the same comparison
 * piececheck.mjs makes without a browser -- run here through the worklet,
 * the port, and a real audio thread's quanta, which is the part
 * piececheck.mjs cannot see.
 *
 * Offline rather than live because offline is repeatable; the live path
 * differs only in who asks for the next quantum and when.
 *
 * Exit status is the number of browsers that failed.
 */

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

import { chromium, firefox } from 'playwright';

import { tapeBefore, tapeLine } from '../tape.mjs';
import { SECONDS, firstDifference, instruments, pieces, reference }
    from './piececheck.mjs';
import { renderDirect } from './render.mjs';
import { serve } from './serve.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const build = path.resolve(process.argv[2] ??
                           path.join(here, '..', '..', 'build-web'));

const { default: createThinkWeb } =
    await import(pathToFileURL(path.join(build, 'thinkweb.js')).href);

const RATE = 48000;
const FRAMES = RATE * 2;
const PATCHES = ['ts1.dsp', 'hat0.dsp', 'amb01.dsp'];

/* The two windows M2's gate names. 256 is the page's; 128 is a window as
   short as the quantum, which is where a step-size bug would show first. */
const WINDOWS = [256, 128];

const nodeBuild = path.resolve(process.argv[3] ??
                               process.env.THINK_WASM_BUILD ??
                               path.join(here, '..', '..', 'build-wasm'));

/* A phrase, not a note: overlapping voices, a re-press, stamps that fall
   mid-window and on a window's first frame. */
const EVENTS = [
    { on: true,  frame: 0,     note: 60, velocity: 100 },
    { on: true,  frame: 1000,  note: 64, velocity: 90 },
    { on: true,  frame: 12288, note: 67, velocity: 80 },
    { on: false, frame: 24000, note: 60 },
    { on: true,  frame: 30000, note: 60, velocity: 110 },
    { on: false, frame: 50000, note: 64 },
    { on: false, frame: 60000, note: 67 },
    { on: false, frame: 70000, note: 60 },
];

async function inBrowser (page, text)
{
    return page.evaluate(async ({ text, frames, rate, events }) =>
    {
        const { createSynth } = await import('./host.js');
        const ctx = new OfflineAudioContext({ numberOfChannels: 2,
                                              length: frames,
                                              sampleRate: rate });
        const logs = [];
        const synth = await createSynth(ctx, { windowlen: 256,
                                               onLog: (s) => logs.push(s) });

        synth.node.connect(ctx.destination);

        const ok = await synth.load(text);

        for (const e of events)
        {
            if (e.on)
                synth.noteOn(e.note, e.velocity, e.frame);
            else
                synth.noteOff(e.note, e.frame);
        }

        await synth.flush();

        const buf = await ctx.startRendering();
        const l = buf.getChannelData(0), r = buf.getChannelData(1);
        const out = new Array(frames * 2);

        for (let i = 0; i < frames; i++)
        {
            out[2 * i] = l[i];
            out[2 * i + 1] = r[i];
        }

        return { ok, logs, out, windowlen: synth.windowlen };
    }, { text, frames: FRAMES, rate: RATE, events: EVENTS });
}

/* A piece, composed in the page's own way: the instruments handed over
   first, the .gen loaded, Play pressed, and the tape collected as it is
   posted. The render is offline, so the transport runs as fast as the
   engine can render it rather than in a minute of real time.
 *
 * The flush at the end is what makes the last batch arrive: a ping is
 * answered after every tape message already posted, and answering it posts
 * the batch in hand (worklet.js). */
async function pieceInBrowser (page, gen, dsps, windowlen)
{
    return page.evaluate(async ({ gen, dsps, windowlen, seconds, rate }) =>
    {
        const { createSynth } = await import('./host.js');
        const ctx = new OfflineAudioContext({
            numberOfChannels: 2,
            length: Math.ceil((seconds + 1) * rate),
            sampleRate: rate,
        });
        const logs = [];
        const events = [];
        const synth = await createSynth(ctx, { windowlen,
                                               onLog: (t) => logs.push(t),
                                               onTape: (m) =>
                                                   events.push(...m.events) });

        synth.node.connect(ctx.destination);

        for (const [name, text] of Object.entries(dsps))
            synth.instrument(name, text);

        const piece = await synth.loadPiece(gen);

        if (piece.errors.length > 0)
            return { ok: false, logs, errors: piece.errors, events: [] };

        /* Started, and acknowledged before the render begins. `transport'
           is a bare postMessage, and an OfflineAudioContext can render the
           whole minute in one go before a control message still in flight
           reaches the worklet. The piece then never starts: drain() finds
           nothing, the ping below posts an empty tape, and the pong comes
           back as if all were well -- a pass-shaped answer of zero events
           rather than an error, which is what `tide.gen has nothing' was.
           Every other step of this setup already waits for the worklet to
           answer; this one has to as well. */
        synth.transport('start');
        await synth.flush();

        await ctx.startRendering();
        await synth.flush();

        return { ok: true, logs, errors: [], events,
                 knobs: piece.knobs.length, windowlen: synth.windowlen };
    }, { gen, dsps, windowlen, seconds: SECONDS, rate: RATE });
}

/* The reference tapes are genwav.mjs's, out of the Node module: checked
   for up front, as piececheck.mjs checks, rather than found missing by an
   uncaught throw halfway through the first browser. */
if (!fs.existsSync(path.join(nodeBuild, 'thinksynth.mjs')))
{
    process.stdout.write(
        `browsertest: no Node module in ${nodeBuild}. It is the tape the ` +
        'pieces are compared against;\n             build it first -- ' +
        'see the top of wasm/CMakeLists.txt.\n');
    process.exit(1);
}

const server = await serve(build, 0);
const url = `http://127.0.0.1:${server.address().port}/`;
const dsps = instruments(build);
let failed = 0;

/* Once, not once per browser: a minute of each piece rendered under Node. */
const seededPieces = pieces(build).filter((p) => p.seeded);
const references = new Map(
    seededPieces.map((p) => [p.name, reference(p.name, nodeBuild)]));

for (const [label, type] of [['chromium', chromium], ['firefox', firefox]])
{
    let browser;

    try
    {
        browser = await type.launch();
    }
    catch (e)
    {
        process.stdout.write(`FAIL  ${label}: could not launch -- ` +
                             `${e.message.split('\n')[0]}\n`);
        failed++;
        continue;
    }

    const page = await browser.newPage();
    const errors = [];

    page.on('pageerror', (e) => errors.push(e.message));
    await page.goto(url);

    let ok = true;

    for (const patch of PATCHES)
    {
        const text = fs.readFileSync(path.join(build, 'dsp', patch), 'utf8');
        const want = await renderDirect(createThinkWeb,
                                        { rate: RATE, text, events: EVENTS,
                                          frames: FRAMES });
        let got;

        try
        {
            got = await inBrowser(page, text);
        }
        catch (e)
        {
            process.stdout.write(`FAIL  ${label} ${patch}: ` +
                                 `${e.message.split('\n')[0]}\n`);
            ok = false;
            continue;
        }

        let first = -1, count = 0, peak = 0;

        for (let i = 0; i < want.out.length; i++)
        {
            peak = Math.max(peak, Math.abs(want.out[i]));

            if (Math.fround(got.out[i]) !== want.out[i])
            {
                if (first < 0)
                    first = i >> 1;
                count++;
            }
        }

        if (!got.ok || peak === 0 || count > 0)
        {
            ok = false;
            process.stdout.write(`FAIL  ${label} ${patch}: ` +
                (!got.ok ? `did not load: ${got.logs.join(' / ')}`
                 : peak === 0 ? 'silent'
                 : `${count} samples differ, the first at frame ${first}`) +
                '\n');
        }
        else
            process.stdout.write(`ok    ${label} ${patch}: ${FRAMES} frames ` +
                                 `identical, peak ${peak.toFixed(3)}\n`);
    }

    for (const piece of seededPieces)
    {
        const want = references.get(piece.name);
        const cells = [];

        for (const windowlen of WINDOWS)
        {
            let got;

            try
            {
                got = await pieceInBrowser(page, piece.text, dsps, windowlen);
            }
            catch (e)
            {
                cells.push(`${windowlen}: ${e.message.split('\n')[0]}`);
                continue;
            }

            if (!got.ok)
            {
                cells.push(`${windowlen}: did not load -- ` +
                           got.errors.join('; '));
                continue;
            }

            const tape = tapeBefore(got.events.map(tapeLine).join(''),
                                    SECONDS);

            cells.push(tape === want ? `${windowlen} ok`
                       : `${windowlen} DIFFERS -- ` +
                         firstDifference(want, tape));
        }

        const bad = cells.some((c) => !c.endsWith('ok'));

        if (bad)
            ok = false;

        process.stdout.write(
            `${bad ? 'FAIL' : 'ok  '}  ${label} ${piece.name.padEnd(14)} ` +
            `${String(want.split('\n').length - 1).padStart(5)} events   ` +
            `${cells.join('   ')}\n`);
    }

    for (const e of errors)
    {
        process.stdout.write(`FAIL  ${label}: page error: ${e}\n`);
        ok = false;
    }

    if (!ok)
        failed++;

    await browser.close();
}

/* close() alone waits for the browsers' keep-alive connections, which
   outlive the browsers here and keep the process up. */
server.closeAllConnections();
server.close();

process.stdout.write(`\n${failed === 0 ? 'all browsers passed'
                                       : failed + ' browser(s) failed'}\n`);
process.exitCode = failed;
