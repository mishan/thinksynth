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
 * chatcheck.mjs -- the chat box, in a browser, against a relay played by
 * this script.
 *
 *   cd wasm/web && npm ci && npx playwright install chromium
 *   node chatcheck.mjs
 *
 * No module, no build directory, no relay: chat.js on a page of its own,
 * with lines typed into its box and the relay's answers handed to it in
 * whatever order and at whatever delay a test wants. What a line does when
 * the relay is slow, refuses it or is not there is decided here, and a
 * room with a real relay (jamtest.mjs) only ever sees the quick answer.
 *
 * Exit status is the number of failures.
 */

/* Under scripts/headless.sh unless THINK_TEST_HEADLESS=0: see headless.mjs. */
import './headless.mjs';

import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { chromium } from 'playwright';

const here = path.dirname(fileURLToPath(import.meta.url));

/* How long the page waits for an echo here: short, so a timeout is quick
   to reach and quick to stay clear of. */
const WAIT = 300;

let failures = 0;

function check (cond, what)
{
    if (cond)
        process.stdout.write(`ok    ${what}\n`);
    else
    {
        failures++;
        process.stdout.write(`FAIL  ${what}\n`);
    }
}

/* `window.t.sent' is every line handed to the relay, as [text, n];
   `t.connected' says whether there is a relay to hand one to. */
const FIXTURE = `<!doctype html>
<html lang="en"><head><meta charset="utf-8"><title>chat</title></head>
<body>
<ul id="feed"></ul>
<form id="form"><input id="input" type="text"><button>Send</button></form>
<p id="note"></p>
<script type="module">
import { createChat } from './chat.js';

const t = { sent: [], connected: true, title: 'Chat' };

t.chat = createChat({
    feed: document.getElementById('feed'),
    form: document.getElementById('form'),
    input: document.getElementById('input'),
    note: document.getElementById('note'),
    send: (text, n) => t.connected && t.sent.push([text, n]) > 0,
    self: () => 'me',
    colorOf: () => 'red',
    visible: () => true,
    title: (text) => { t.title = text; },
    wait: ${WAIT},
});

/* The relay's copy of line n, back to its sender. */
t.echo = (n) =>
{
    const [text] = t.sent.find(([, m]) => m === n) ?? [''];

    t.chat.said({ from: 'me', name: 'me', text, n });
};

t.box = () => document.getElementById('input').value;
t.note = () => document.getElementById('note').textContent;
t.lines = () => [...document.querySelectorAll('#feed li')]
    .map((li) => li.textContent);

window.t = t;
</script>
</body></html>
`;

const server = http.createServer((req, res) =>
{
    const name = new URL(req.url, 'http://localhost').pathname;

    if (name === '/')
    {
        res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
        res.end(FIXTURE);
        return;
    }

    const file = path.join(here, path.basename(name));

    if (!/\.js$/.test(name) || !fs.existsSync(file))
    {
        res.writeHead(404);
        res.end();
        return;
    }

    res.writeHead(200, { 'Content-Type': 'text/javascript; charset=utf-8' });
    res.end(fs.readFileSync(file));
});

await new Promise((go) => server.listen(0, '127.0.0.1', go));

const browser = await chromium.launch();
const page = await browser.newPage();

page.on('pageerror', (e) => check(false, `the page threw: ${e.message}`));

await page.goto(`http://127.0.0.1:${server.address().port}/`);
await page.waitForFunction(() => window.t !== undefined);

const say = async (text) =>
{
    await page.fill('#input', text);
    await page.press('#input', 'Enter');
};
const t = (fn, arg) => page.evaluate(fn, arg);
const pause = (ms) => new Promise((r) => setTimeout(r, ms));

/* A line given up on, put back, and then sent back after all: it went,
   so it leaves the box -- and is not sent a second time by somebody who
   took the box at its word. */
await say('one');
await pause(WAIT * 2);

check(await t(() => window.t.box()) === 'one' &&
      /no chat/.test(await t(() => window.t.note())),
      'a line the relay never sent back is put back in the box, and why');

await t(() => window.t.echo(1));

check(await t(() => window.t.box()) === '' &&
      await t(() => window.t.note()) === '' &&
      (await t(() => window.t.lines())).join() === 'me: one',
      'and when it comes back late, it is shown and taken out of the box');

/* An echo of ours that nothing is waiting for -- a line from before a
   reload, say -- cancels nobody else's wait. */
await say('two');
await say('three');
await t(() => window.t.chat.said({ from: 'me', name: 'me', text: 'old',
                                     n: 99 }));
await t(() => { window.t.echo(2); window.t.echo(3); });
await pause(WAIT * 2);

check(await t(() => window.t.box()) === '' &&
      await t(() => window.t.note()) === '',
      'echoes matched by number leave nothing put back, whatever else ' +
      'came in between');

/* Both of two refused lines come back, in order. */
await say('four');
await say('five');
await t(() =>
{
    window.t.chat.refused({ n: 4, why: 'too fast; wait a moment' });
    window.t.chat.refused({ n: 5, why: 'too fast; wait a moment' });
});

check(await t(() => window.t.box()) === 'four five' &&
      await t(() => window.t.note()) === 'too fast; wait a moment',
      'every refused line goes back into the box, with the reason');

await pause(WAIT * 2);

check(await t(() => window.t.box()) === 'four five',
      'and none of them is put back a second time when its wait runs out');

/* No relay at all: said at once, and the line stays where it was typed. */
await t(() => { window.t.connected = false; });
await page.fill('#input', '');
await say('six');

check(await t(() => window.t.box()) === 'six' &&
      await t(() => window.t.note()) === 'Not connected to the relay.',
      'with no connection, the box keeps the line and says so');

/* A seek just short of a minute is a minute, not 0:60.0. */
await t(() => window.t.chat.command('ann', { type: 'transport', op: 'start',
                                               seek: 59.97 }));

check((await t(() => window.t.lines())).at(-1) === 'ann played from 1:00.0',
      'a seek is said in the clock\'s own rounding');

/* A name that reverses what follows it is kept to itself. */
await t(() => window.t.chat.said({ from: 'x', name: '‮evil',
                                     text: 'hello' }));

check(await t(() => document.querySelector('#feed li:last-child bdi')
                        ?.textContent === '‮evil'),
      'a name is isolated from the text after it');

await browser.close();
server.close();

process.exitCode = failures;
