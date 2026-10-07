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
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * worklettime.mjs -- what the worklet's process() calls cost on the solo
 * page, playing a piece in a real-time AudioContext.
 *
 *   node wasm/web/worklettime.mjs [--browser chromium|firefox]
 *       [--seconds 30] [--json out.json] [BUILD_DIR] [piece.gen...]
 *
 * bench.mjs times the engine in Node, which is the cost and nothing else.
 * This is the same pieces as a listener gets them: the page loads one,
 * presses Play, and the worklet counts its own calls (worklet.js, the
 * calls over the quantum's budget and a histogram by millisecond), which
 * host.js sums and window.solo.quanta hands out. For each piece it prints
 * the calls over budget per minute of play, the slowest call and the p99,
 * and the audio clock against the wall clock, which is what says the
 * context really ran in real time and did not fall behind or stop.
 *
 * Every piece by default. Not a gate: a count of calls over budget is a
 * property of the machine, its load and its browser as much as of the
 * code, and the clock the worklet times itself by is Date.now, a whole
 * millisecond (worklet.js says what that means for the count). Under
 * scripts/headless.sh, as the browser tests are, so the context plays
 * into a null sink that is still clocked like a device.
 */

/* Under scripts/headless.sh unless THINK_TEST_HEADLESS=0: see headless.mjs. */
import './headless.mjs';

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';

import { chromium, firefox } from 'playwright';

import { serve } from './serve.mjs';

/* What host.js's recent load figures span, RECENT_BATCHES at 44.1 kHz and
   then some. */
const RECENT_S = 3;

const here = path.dirname(fileURLToPath(import.meta.url));
const { values: opts, positionals } =
    parseArgs({ options: { browser: { type: 'string', default: 'chromium' },
                           seconds: { type: 'string', default: '30' },
                           json: { type: 'string' } },
                allowPositionals: true });
const build = path.resolve(positionals[0] ??
                           path.join(here, '..', '..', 'build-web'));
const SECONDS = Number(opts.seconds);
const engine = { chromium, firefox }[opts.browser];

/* Whole seconds: play() takes a reading a second. */
if (!(Number.isInteger(SECONDS) && SECONDS > 0))
{
    process.stderr.write(
        `worklettime: --seconds ${opts.seconds} is not a positive whole number\n`);
    process.exit(2);
}

if (engine === undefined)
{
    process.stderr.write(`worklettime: no browser '${opts.browser}'\n`);
    process.exit(2);
}

/* Opened now, so a path that cannot be written is known before the
   minutes of playing rather than after. */
const jsonOut = opts.json !== undefined ? fs.openSync(opts.json, 'w') : null;

const names = positionals.length > 1 ? positionals.slice(1)
    : JSON.parse(fs.readFileSync(path.join(build, 'gen', 'index.json'),
                                 'utf8'));

/* The whole millisecond 99% of the calls took no longer than, from the
   worklet's histogram; the last bin is that many or more. */
function p99 (took, calls)
{
    let ms = 0;

    for (let seen = took[0] ?? 0; seen < 0.99 * calls; seen += took[ms] ?? 0)
        ms++;

    return ms;
}

/* The calls and the histogram between two readings of
   window.solo.quanta. */
function since (a, b)
{
    return { calls: b.calls - a.calls, overBudget: b.overBudget - a.overBudget,
             took: b.took.map((n, ms) => n - (a.took[ms] ?? 0)) };
}

/* Once a second for `seconds': the wall clock, the audio clock and the
   load figures. The slowest call since the start is the one that built
   the piece's graphs at load, for good, so the slowest of the recent
   calls is kept instead: they span RECENT_S, longer than a reading
   apart. */
async function play (page, seconds)
{
    return page.evaluate(async (S) =>
    {
        const s = window.solo;
        const read = () => ({ wallS: performance.now() / 1000,
                              audioS: s.audioTime(),
                              quanta: s.quanta() });
        const out = [read()];
        let slowestMs = 0;

        for (let i = 0; i < S; i++)
        {
            await new Promise((go) => setTimeout(go, 1000));
            out.push(read());
            slowestMs = Math.max(slowestMs, out.at(-1).quanta.recent.slowestMs);
        }

        return { readings: out, slowestMs };
    }, seconds);
}

const site = await serve(build, 0, '127.0.0.1', null);
const url = `http://127.0.0.1:${site.address().port}/index.html?panes=0`;
const browser = await engine.launch(opts.browser === 'chromium'
    ? { args: ['--autoplay-policy=no-user-gesture-required'] } : {});
const version = browser.version();
const results = [];
let failures = 0;

process.stdout.write(
    `${opts.browser} ${version}, ${SECONDS} s a piece\n\n` +
    'piece            over/min   slowest   p99   audio/wall   worst 1 s\n');

try
{
    for (const name of names)
    {
        const page = await browser.newPage();
        const errors = [];

        /* The console's too: an exception in the worklet's own scope is
           no pageerror, and it is the one that silences the piece. */
        page.on('pageerror', (e) => errors.push(e.message));
        page.on('console', (m) =>
        {
            if (m.type() === 'error')
                errors.push(m.text());
        });

        try
        {
            await page.goto(url);
            await page.waitForFunction(
                () => document.getElementById('range').textContent !== '');
            await page.selectOption('#mode', 'piece');
            await page.selectOption('#piece', name);
            await page.click('#start');
            await page.waitForFunction(
                () => !document.getElementById('loadpiece').disabled,
                null, { timeout: 60000 });
            await page.evaluate(() => window.solo.settled());

            /* For the load's quanta to leave the recent figures before
               Play, since nothing could be heard to break in them. */
            await page.waitForTimeout(RECENT_S * 1000);
            await page.waitForFunction(
                () => !document.getElementById('play').disabled,
                null, { timeout: 60000 });
            await page.click('#play');
            await page.waitForFunction(
                () => window.solo.quanta()?.calls > 0,
                null, { timeout: 60000 });

            const { readings, slowestMs } = await play(page, SECONDS);
            const first = readings[0], last = readings.at(-1);
            const q = since(first.quanta, last.quanta);
            const wallS = last.wallS - first.wallS;
            const audioS = last.audioS - first.audioS;

            /* The worst second's audio against its wall second: a context
               that stalled and caught up shows here and not in the ratio
               over the whole run. */
            let worstSecondS = 0;

            for (let i = 1; i < readings.length; i++)
            {
                const a = readings[i - 1], b = readings[i];

                worstSecondS = Math.max(worstSecondS,
                    Math.abs((b.audioS - a.audioS) - (b.wallS - a.wallS)));
            }

            const r = {
                piece: name, calls: q.calls, overBudget: q.overBudget,
                overBudgetPerMin: q.overBudget / (wallS / 60),
                slowestMs, p99Ms: p99(q.took, q.calls), took: q.took,
                coarseClock: last.quanta.coarseClock,
                wallS, audioS, audioOverWall: audioS / wallS, worstSecondS,
                errors };

            results.push(r);
            process.stdout.write(
                `${name.padEnd(15)} ${r.overBudgetPerMin.toFixed(1).padStart(9)} ` +
                `${`${slowestMs} ms`.padStart(9)} ${`${r.p99Ms} ms`.padStart(5)} ` +
                `${r.audioOverWall.toFixed(4).padStart(12)} ` +
                `${(worstSecondS * 1000).toFixed(0).padStart(7)} ms` +
                (r.coarseClock ? '   the clock is too coarse to trust' : '') +
                '\n');

            /* Timed, and the numbers kept, but not a run to trust. */
            if (errors.length > 0)
            {
                failures++;
                r.error = 'the page reported errors';
                process.stdout.write(`${name}: ${r.error}: ` +
                                     `${errors.join('; ')}\n`);
            }
        }
        catch (e)
        {
            failures++;
            results.push({ piece: name, error: e.message.split('\n')[0],
                           errors });
            process.stdout.write(`${name}: ${e.message.split('\n')[0]}\n`);
        }

        await page.close();
    }
}
finally
{
    await browser.close();
    site.close();
}

if (jsonOut !== null)
    fs.writeFileSync(jsonOut, JSON.stringify({
        browser: opts.browser, version, seconds: SECONDS,
        build, results }, null, 2) + '\n');

process.exit(failures);
