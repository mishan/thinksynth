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
 * pairlinktest.mjs -- a phone and its page linked through pair.js and
 * mesh.js, on a relay in this process.
 *
 *   node wasm/web/pairlinktest.mjs
 *
 * Node has no WebRTC, so this is the path through the relay: the pairing
 * made and joined, frames both ways, a lossy one skipped behind a socket
 * that hasn't sent, a side's socket cut and coming back by itself to a
 * fresh link, an end, and pairings that can't be. The direct path is
 * linktest.mjs's, in a browser.
 *
 * Exit status is the number of failures.
 */

import { Mesh } from './mesh.js';
import { Pair } from './pair.js';
import { relay } from '../../relay/relay.mjs';

let failures = 0;

function check (cond, what, detail = '')
{
    process.stdout.write(`${cond ? 'ok   ' : 'FAIL '} ${what}` +
                         `${!cond && detail ? `: ${detail}` : ''}\n`);
    failures += cond ? 0 : 1;
}

/* `f' true within `ms', or false. */
async function until (f, ms = 3000)
{
    for (const end = Date.now() + ms; Date.now() < end;)
    {
        if (f())
            return true;
        await new Promise((r) => setTimeout(r, 20));
    }
    return f();
}

/* A side: its Pair, the mesh over it, and everything each has said. */
function side (url, how)
{
    const s = { got: [], events: [] };

    s.pair = new Pair(url, how);
    for (const type of ['paired', 'token', 'peer', 'ended', 'refused'])
        s.pair.on(type, (...args) => s.events.push([type, ...args]));
    s.mesh = new Mesh(s.pair, (from, cmd) => s.got.push([from, cmd]));
    s.event = (type) => s.events.find((e) => e[0] === type);
    return s;
}

const q = await relay({ port: 0, host: '127.0.0.1' });
const url = `ws://127.0.0.1:${q.address().port}/pair`;

try
{
    const host = side(url, { make: true });

    await until(() => host.event('paired'));

    const [, code, token] = host.event('paired') ?? [];

    check(typeof code === 'string' && typeof token === 'string',
          'the page is given a code and a token to keep');

    const phone = side(url, { code, what: 'a phone' });

    check(await until(() => phone.event('token') && host.event('peer')) &&
          host.event('peer')[1] === true && host.event('peer')[2] === 'a phone',
          'the phone joins, and the page is told what it said it is');
    check(host.mesh.status('phone').path === 'relayed' &&
          phone.mesh.status('host').path === 'relayed',
          'with no WebRTC, each side reaches the other through the relay');

    phone.mesh.broadcast({ type: 'frame', seq: 1 }, false);
    phone.mesh.broadcast({ type: 'frame', seq: 2 }, true);
    host.mesh.broadcast({ type: 'hello', n: 7 });
    check(await until(() => host.got.length === 2 && phone.got.length === 1) &&
          host.got.map(([f, c]) => `${f}:${c.seq}`).join() === 'phone:1,phone:2' &&
          phone.got[0][1].n === 7,
          'frames reach the other side both ways, either kind');

    /* A lossy frame behind one the socket hasn't sent is skipped; one
       that mustn't be lost isn't. */
    host.got.length = 0;
    Object.defineProperty(phone.pair.ws, 'bufferedAmount',
                          { value: 1, configurable: true });
    phone.mesh.broadcast({ type: 'frame', seq: 3 }, false);
    phone.mesh.broadcast({ type: 'frame', seq: 4 }, true);
    delete phone.pair.ws.bufferedAmount;
    check(await until(() => host.got.length >= 1) &&
          (await new Promise((r) => setTimeout(r, 200)),
           host.got.map(([, c]) => c.seq).join() === '4'),
          'a lossy frame behind an unsent one is skipped, a sure one is sent',
          host.got.map(([, c]) => c.seq).join());

    /* Each side's socket cut at the relay in turn: it comes back with its
       token, and both sides link afresh. */
    for (const [cut, other, name] of [[phone, host, 'phone'], [host, phone, 'page']])
    {
        const was = [host.mesh.links.get('phone'), phone.mesh.links.get('host')];

        other.got.length = 0;
        other.events.length = 0;
        q.pairs.pairs.get(code)[cut === host ? 'host' : 'phone'].ws.terminate();
        check(await until(() => other.events.some((e) => e[0] === 'peer' && !e[1]) &&
                                other.events.some((e) => e[0] === 'peer' && e[1])) &&
              host.mesh.links.get('phone') !== was[0] &&
              phone.mesh.links.get('host') !== was[1],
              `the ${name} cut at the relay is gone, then back by itself, and ` +
              'both sides link afresh');
        cut.mesh.broadcast({ type: 'frame', seq: 5 }, true);
        check(await until(() => other.got.length === 1) && other.got[0][1].seq === 5,
              'and frames flow again');
    }

    /* A frame sent while this side's socket is down is gone, not kept to
       send once it is back: by then it says what is no longer so. */
    host.got.length = 0;
    host.events.length = 0;
    phone.pair.ws.close();
    phone.mesh.broadcast({ type: 'away', n: 1 }, true);
    await until(() => host.events.some((e) => e[0] === 'peer' && e[1]));
    phone.mesh.broadcast({ type: 'frame', seq: 6 }, true);
    await until(() => host.got.length >= 1);
    check(host.got.map(([, c]) => c.type).join() === 'frame',
          'a frame sent while this side is away is dropped, not kept for later',
          host.got.map(([, c]) => c.type).join());

    /* The phone ends it: the page is told, and the mesh lets it go. */
    phone.pair.end();
    check(await until(() => host.event('ended')) &&
          phone.mesh.status('host').path === 'none' &&
          host.mesh.status('phone').path === 'none',
          'the phone ending it ends it for the page, and both let the link go');

    /* A pairing the relay has let go of while a side was away: coming
       back, that side hears it has ended. */
    const h2 = side(url, { make: true });

    await until(() => h2.event('paired'));

    const p2 = side(url, { code: h2.event('paired')[1] });

    await until(() => p2.event('token'));
    q.pairs.pairs.delete(h2.event('paired')[1]);
    h2.pair.ws.close();
    check(await until(() => h2.event('ended')) &&
          h2.mesh.status('phone').path === 'none',
          'a side coming back to a pairing the relay has let go of hears it ended');

    /* A phone whose first socket went with the relay's answer: its code
       is spent, so it says to scan again rather than waiting. */
    const h3 = side(url, { make: true });

    await until(() => h3.event('paired'));

    const p3 = side(url, { code: h3.event('paired')[1] });

    p3.pair.on('token', () => {});
    p3.pair.heard = () => {};       /* the answer never heard */
    await until(() => q.pairs.pairs.get(h3.event('paired')[1])?.joined);
    p3.pair.ws.close();
    check(await until(() => p3.event('refused')) &&
          /scan again/.test(p3.event('refused')[1]),
          'a phone lost before its token is told to scan again',
          p3.event('refused')?.[1]);

    /* Signals by the connection they're for, fed straight in: one left
       over from a connection gone is dropped, one from a connection the
       other side began unannounced makes this side begin one too. */
    {
        const p = new Pair('ws://127.0.0.1:1/pair', { code: 'c', token: 't' });
        const seen = [];
        let links = 0;

        p.over = true;                  /* no socket to come back with */
        p.on('signal', (from, d) => seen.push(d));
        p.on('joined', () => links++);
        p.heard({ type: 'peer', here: true, what: '' });
        p.heard({ type: 'signal', data: { session: 'A', data: 1 } });
        p.heard({ type: 'peer', here: true, what: '' });
        p.heard({ type: 'signal', data: { session: 'A', data: 2 } });
        p.heard({ type: 'signal', data: { session: 'B', data: 3 } });

        const before = links;

        p.heard({ type: 'signal', data: { session: 'C', data: 4 } });
        p.heard({ type: 'signal', data: { session: 'B', data: 5 } });
        check(seen.join() === '1,3,4' && before === 2 && links === 3,
              'a signal left over from a connection gone is dropped, and one ' +
              'from a connection begun unannounced begins one here',
              `${seen.join()}; ${before} then ${links} links`);
    }

    const stranger = side(url, { code: 'x'.repeat(22) });

    check(await until(() => stranger.event('refused')) &&
          stranger.event('refused')[1] === 'unknown',
          'a code the relay doesn\'t know is refused');

    const nowhere = side('ws://127.0.0.1:1/pair', { make: true });

    check(await until(() => nowhere.event('refused')),
          'a relay that can\'t be reached is said so, not waited on');
}
catch (e)
{
    check(false, `threw: ${e.stack}`);
}

q.shutdown();
process.stdout.write(`\n${failures === 0 ? 'the link does what it says'
                                          : `${failures} failed`}\n`);
process.exit(failures);
