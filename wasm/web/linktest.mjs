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
 * linktest.mjs -- a phone and its page linked directly, in Chromium: two
 * pages paired through a relay in this process (pair.js), WebRTC between
 * them (mesh.js).
 *
 *   node wasm/web/linktest.mjs
 *
 * The link opens direct; frames go both ways on it, each kind on its own
 * channels; and a phone page reloaded, or the page itself, coming back
 * with its token, is direct again at once rather than after the mesh gives
 * up on the old connection. The relayed path is pairlinktest.mjs's, in Node.
 *
 * Exit status is the number of failures.
 */

import './headless.mjs';

import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { chromium } from 'playwright';

import { relay } from '../../relay/relay.mjs';
import { serve } from './serve.mjs';

let failures = 0;

function check (cond, what, detail = '')
{
    process.stdout.write(`${cond ? 'ok   ' : 'FAIL '} ${what}` +
                         `${!cond && detail ? `: ${detail}` : ''}\n`);
    failures += cond ? 0 : 1;
}

const here = path.dirname(fileURLToPath(import.meta.url));
const site = await serve(here, 0, '127.0.0.1');
const q = await relay({ port: 0, host: '127.0.0.1' });
const relayUrl = `ws://127.0.0.1:${q.address().port}/pair`;
const browser = await chromium.launch();

/* A page holding one side: a Pair and a Mesh over it, what it hears, and
   which channel each frame came on. */
async function open (how)
{
    const page = await browser.newPage();

    await page.goto(`http://127.0.0.1:${site.address().port}/index.html`,
                    { waitUntil: 'commit' });
    await page.evaluate(async ({ relayUrl, how }) =>
    {
        const { Pair } = await import('/pair.js');
        const { Mesh } = await import('/mesh.js');
        const s = { got: [], channels: [] };

        s.pair = new Pair(relayUrl, how);
        s.pair.on('paired', (code, token) => { s.code = code; s.token = token; });
        s.pair.on('token', (token) => { s.token = token; });
        s.mesh = new Mesh(s.pair, (from, cmd) => s.got.push(cmd));
        /* Which channel each frame rode in on. */
        const attach = s.mesh.attach.bind(s.mesh);

        s.mesh.attach = (peer, l, channel) =>
        {
            channel.addEventListener('message', (e) =>
            {
                const m = JSON.parse(e.data);

                if (m.seq !== undefined)
                    s.channels.push(`${m.seq}:${channel.label}`);
            });
            return attach(peer, l, channel);
        };
        window.side = s;
    }, { relayUrl, how });
    return page;
}

const direct = (page) => page.waitForFunction(
    () => window.side.mesh.status(window.side.pair.other).path === 'direct',
    null, { timeout: 15000 }).then(() => true, () => false);

try
{
    const host = await open({ make: true });

    await host.waitForFunction(() => window.side.code !== undefined);

    const code = await host.evaluate(() => window.side.code);
    let phone = await open({ code, what: 'a phone' });

    check(await direct(host) && await direct(phone),
          'the two pages link directly');

    await phone.evaluate(() =>
    {
        window.side.mesh.broadcast({ type: 'frame', seq: 1 }, false);
        window.side.mesh.broadcast({ type: 'frame', seq: 2 }, true);
    });
    await host.evaluate(() => window.side.mesh.broadcast({ type: 'hi', n: 3 }));
    await host.waitForFunction(() => window.side.got.length >= 2, null,
                               { timeout: 5000 }).catch(() => {});
    await phone.waitForFunction(() => window.side.got.length >= 1, null,
                                { timeout: 5000 }).catch(() => {});
    await new Promise((r) => setTimeout(r, 300));

    const channels = (await host.evaluate(() => window.side.channels)).sort();

    check((await phone.evaluate(() => window.side.got[0]?.n)) === 3 &&
          channels.join() === '1:gestures,2:gestures,2:keys',
          'frames go both ways directly: a lossy one on the channel that drops, ' +
          'a sure one on both',
          channels.join());

    /* The phone's page reloaded: a new page, the same code and its token. */
    const token = await phone.evaluate(() => window.side.token);

    await phone.close();
    phone = await open({ code, token });

    const t0 = Date.now();
    const again = await direct(host) && await direct(phone);
    const took = Date.now() - t0;

    check(again && took < 4000,
          'a phone page reloaded is direct again at once, on a fresh connection',
          `${took} ms`);

    await phone.evaluate(() => window.side.mesh.broadcast({ type: 'frame', seq: 9 }, true));
    check(await host.waitForFunction(
        () => window.side.got.some((c) => c.seq === 9), null, { timeout: 5000 })
              .then(() => true, () => false),
          'and frames flow on it');

    /* The page itself reloaded, back with its own token. */
    const hostToken = await host.evaluate(() => window.side.token);

    await host.close();

    const host2 = await open({ code, token: hostToken, host: true });
    const t1 = Date.now();
    const again2 = await direct(host2) && await direct(phone);
    const took2 = Date.now() - t1;

    check(again2 && took2 < 4000,
          'a page reloaded is direct again at once too', `${took2} ms`);
    await host2.evaluate(() => window.side.mesh.broadcast({ type: 'hi', n: 10 }));
    check(await phone.waitForFunction(
        () => window.side.got.some((c) => c.n === 10), null, { timeout: 5000 })
              .then(() => true, () => false),
          'and the phone hears it');
}
catch (e)
{
    check(false, `threw: ${e.stack}`);
}

await browser.close();
q.shutdown();
site.close();
process.stdout.write(`\n${failures === 0 ? 'the direct link does what it says'
                                          : `${failures} failed`}\n`);
process.exit(failures);
