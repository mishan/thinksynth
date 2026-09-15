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
 * browsertest.mjs -- the worklet, in real browsers, held against the module
 * run directly.
 *
 *   cd wasm/web && npm ci && npx playwright install chromium firefox
 *   node browsertest.mjs [BUILD_DIR]
 *
 * For each browser: serve the site, build the synth exactly as the page
 * does (host.js) but on an OfflineAudioContext, play a phrase with stamped
 * notes, render, and compare every sample with what render.mjs gets from
 * the same module called from Node. It is one wasm file on both sides, so
 * the two agree to the bit or the plumbing between them is wrong -- the
 * module handed to the worklet, the messages, the stamps, the 128-frame
 * quanta, the de-interleave.
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

import { renderDirect } from './render.mjs';
import { serve } from './serve.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const build = path.resolve(process.argv[2] ??
                           path.join(here, '..', '..', 'build-web'));

const { default: createThinkWeb } =
    await import(pathToFileURL(path.join(build, 'thinkweb.mjs')).href);

const RATE = 48000;
const FRAMES = RATE * 2;
const PATCHES = ['ts1.dsp', 'hat0.dsp', 'amb01.dsp'];

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

const server = await serve(build, 0);
const url = `http://127.0.0.1:${server.address().port}/`;
let failed = 0;

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
