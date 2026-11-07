#!/usr/bin/env node
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
 * sdkcheck.mjs -- the SDK (thinksynth.js, docs/SDK.md) as a game uses it,
 * in a headless Chromium, from the packaged directory alone.
 *
 *   node wasm/web/sdkcheck.mjs [build-web/sdk]
 *
 * The claim it holds the SDK to is the look-ahead's: every note the
 * worklet plays was announced ahead, at the same time and pitch, and every
 * note announced was played -- through a mute, an unmute, a stage put in
 * and a stage taken out, each stamped by the SDK on its line. Phases are
 * timed in frames the worklet rendered, not wall-clock time.
 */

import './headless.mjs';

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { chromium } from 'playwright';

import { serve } from './serve.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const sdk = path.resolve(process.argv[2] ??
                         path.join(here, '..', '..', 'build-web', 'sdk'));

let failures = 0;

function check (cond, what)
{
    process.stdout.write(`${cond ? 'ok  ' : 'FAIL'}  ${what}\n`);
    failures += cond ? 0 : 1;
}

if (!fs.existsSync(path.join(sdk, 'thinksynth.js')))
{
    process.stdout.write(`sdkcheck: no SDK in ${sdk}; build it first ` +
                         '-- cmake --build build-web --target sdk.\n');
    process.exit(1);
}

/* The page a game would be: nothing but the SDK. */
const PAGE = `<!doctype html><meta charset="utf-8"><script type="module">
import { createEngine } from './thinksynth.js';

const log = [];
const fetched = [];
const real = window.fetch;

window.fetch = (u, o) => { fetched.push(String(u)); return real(u, o); };

const ctx = new AudioContext();
const engine = await createEngine(ctx, { onLog: (t) => log.push(t) });
const ahead = [], played = [], lead = [], lines = {};
const wait = (bars) =>
{
    const until = engine.tapeFrame + bars * engine.barSeconds * engine.rate;

    return new Promise((ok) =>
    {
        const poll = () => engine.tapeFrame >= until ? ok()
                                                     : setTimeout(poll, 10);
        poll();
    });
};
const piece = async (name) => (await fetch(\`gen/\${name}\`)).text();
const tape = engine.tape.bind(engine);

engine.tape = (m) =>
{
    lead.push(...(m.keys ?? []).filter((k) => k.on &&
        k.channel === engine.instruments.get('lead')));
    tape(m);
};

await engine.load(await piece('outrun.gen'),
                  { voices: { lead: 'dsp "analog.dsp"; amp = 18;' } });
engine.on('ahead', (n) => ahead.push(...n));
engine.on('played', (n) => played.push(...n));

let early = null;

try
{
    engine.play('lead', 76);
}
catch (e)
{
    early = e.message;
}

await engine.start({ fromBar: 8, setup: (e) => e.mute('hats') });

await wait(1.5);
lines.unmute = engine.line();
engine.mute('hats', false);
lines.flood = await engine.addStage('bass', 'flood', 'ratchet',
                                    { count: 2, prob: 1, decay: 0.8 });
await wait(3);
lines.unflood = await engine.removeStage('bass', 'flood');
lines.lead = engine.play('lead', 76, { quantize: 0.25 });
await wait(3);
engine.stop();

const result = { ahead, played, lead, lines, log, early,
                 fetched: [...fetched], barSeconds: engine.barSeconds,
                 text: engine.text, reached: engine.written() };

/* A piece that names one graph from several instruments. */
result.again = await engine.load(await piece('attract.gen'))
    .then(() => engine.chains.length, (e) => String(e));
window.result = result;
</script>`;

const site = await serve(sdk, 0, '127.0.0.1', null);
const base = `http://127.0.0.1:${site.address().port}/`;
const browser = await chromium.launch(
    { args: ['--autoplay-policy=no-user-gesture-required'] });

try
{
    const page = await browser.newPage();
    const errors = [];

    page.on('pageerror', (e) => errors.push(e.message));
    await page.route(`${base}check.html`, (r) =>
        r.fulfill({ contentType: 'text/html', body: PAGE }));
    await page.goto(`${base}check.html`);
    await page.waitForFunction(() => window.result !== undefined, null,
                               { timeout: 120000 });

    const r = await page.evaluate(() => window.result);
    const key = (n) => `${n.chain} ${n.at.toFixed(6)} ${n.note}`;
    const announced = new Set(r.ahead.map(key));
    const sounded = new Set(r.played.map(key));
    const last = Math.max(...r.played.map((n) => n.at));
    const missing = r.played.filter((n) => !announced.has(key(n)));
    const unplayed = r.ahead.filter((n) => n.at < last - 0.05 &&
                                           !sounded.has(key(n)));
    const bar = (n) => Math.floor(n.at / r.barSeconds);
    const perBar = (chain, from, to) =>
    {
        const notes = r.played.filter((n) => n.chain === chain &&
                                             n.at >= from && n.at < to);

        return notes.length / ((to - from) / r.barSeconds);
    };

    check(errors.length === 0, `no page errors${errors.length ?
        `: ${errors.join('; ')}` : ''}`);
    check(r.played.length > 100, `${r.played.length} notes played`);
    check(missing.length === 0,
          `every note played was announced ahead (${missing.length} not)`);
    check(unplayed.length === 0,
          `every note announced was played (${unplayed.length} not)`);
    check(!r.played.some((n) => n.chain === 'hats' && n.at < r.lines.unmute),
          'the hats muted at the start stay muted until their line');
    check(r.played.some((n) => n.chain === 'hats' &&
                               bar(n) === bar({ at: r.lines.unmute })),
          `...and play from it, bar ${bar({ at: r.lines.unmute }) + 1}`);
    check(perBar('bass', r.lines.flood, r.lines.unflood) >
          1.5 * perBar('bass', 8 * r.barSeconds, r.lines.flood),
          'a ratchet put into the bass doubles it from its line');
    check(perBar('bass', r.lines.unflood, last) <
          perBar('bass', r.lines.flood, r.lines.unflood),
          '...and taken out, stops at its line');
    check(!r.text.includes('stage flood'), 'the edited text is without it');
    check(r.reached > last, 'the look-ahead is ahead of the worklet');
    check(r.lead.some((n) => n.note === 76 &&
                             Math.abs(n.at - r.lines.lead) < 0.01),
          `a note played on the page's own instrument sounds, at ${
              r.lines.lead.toFixed(3)}`);
    check(r.early !== null, `play() before start() throws: ${r.early}`);
    check(typeof r.again === 'number' && r.again > 0,
          `a second piece that repeats a graph loads: ${r.again} chains`);

    const graphs = r.fetched.filter((u) => /\/dsp\/.*\.dsp$/.test(u));

    check(graphs.length > 0 && graphs.length < 20,
          `only the graphs the piece names are fetched: ${graphs.length}`);
}
finally
{
    await browser.close();
    site.close();
}

process.stdout.write(failures === 0
    ? '\nthe SDK announces what it plays, through every change it makes\n'
    : `\n${failures} failure(s)\n`);
process.exit(failures === 0 ? 0 : 1);
