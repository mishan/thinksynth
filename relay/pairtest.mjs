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
 * pairtest.mjs -- a phone paired with a page through the relay
 * (pair.mjs), driven from Node as the two pages will drive it.
 *
 *   node relay/pairtest.mjs
 *
 * A code is the relay's, joined once; what either side sends reaches the
 * other; a side comes back only with its own token; a code nobody joins
 * and a side gone too long end the pairing, on a clock the test turns;
 * and a pairing's frames, rate, number and site are held to their limits.
 *
 * Exit status is the number of failures.
 */

import WebSocket from 'ws';

import { relay } from './relay.mjs';

let failures = 0;

function check (cond, what)
{
    if (cond)
        process.stdout.write(`ok    ${what}\n`);
    else
    {
        process.stdout.write(`FAIL  ${what}\n`);
        failures++;
    }
}

/* `p', or `fallback' if it hasn't settled in `ms': a socket that should
   close and doesn't fails a check rather than hanging the run. */
function within (p, ms = 2000, fallback = 'not in time')
{
    return Promise.race([p, new Promise((r) => setTimeout(() => r(fallback), ms))]);
}

/* A socket as a page holds one: every message kept, the next of a type
   waited for, and how it closed. */
class Side
{
    constructor (url, headers = {})
    {
        this.ws = new WebSocket(url, { headers });
        this.got = [];
        this.waiting = [];
        this.status = null;
        this.closed = new Promise((r) => this.ws.on('close', (code) => r(code)));
        /* With this heard, ws says nothing else of a refused upgrade. */
        this.refused = new Promise((r) => this.ws.on('unexpected-response',
                                                     (req, res) =>
        {
            this.status = res.statusCode;
            req.destroy();
            r();
        }));
        this.ws.on('error', () => {});
        this.ws.on('message', (d) =>
        {
            const m = JSON.parse(d.toString());

            this.got.push(m);
            for (const w of this.waiting.splice(0))
                if (w.type === m.type)
                    w.resolve(m);
                else
                    this.waiting.push(w);
        });
    }

    open ()
    {
        return new Promise((resolve, reject) =>
        {
            this.ws.on('open', resolve);
            this.ws.on('close', () => reject(new Error(`closed: ${this.status}`)));
            this.refused.then(() => reject(new Error(`refused: ${this.status}`)));
        });
    }

    send (m)
    {
        this.ws.send(typeof m === 'string' ? m : JSON.stringify(m));
    }

    next (type, ms = 2000)
    {
        const k = this.got.findIndex((m) => m.type === type);

        if (k >= 0)
            return Promise.resolve(this.got.splice(k, 1)[0]);

        return new Promise((resolve) =>
        {
            const w = { type, resolve: (m) =>
            {
                clearTimeout(timer);
                this.got.splice(this.got.indexOf(m), 1);
                resolve(m);
            } };
            /* Given up on, it takes nothing that comes later. */
            const timer = setTimeout(() =>
            {
                this.waiting.splice(this.waiting.indexOf(w), 1);
                resolve(null);
            }, ms);

            this.waiting.push(w);
        });
    }

    /* How many of a type have come, once nothing more is coming. */
    async count (type, ms = 300)
    {
        await new Promise((r) => setTimeout(r, ms));
        return this.got.filter((m) => m.type === type).length;
    }

    close ()
    {
        this.ws.close();
        return this.closed;
    }
}

/* A relay, and a way to open a pairing on it and a phone into that. */
async function start (options = {})
{
    const q = await relay({ port: 0, host: '127.0.0.1', ...options });
    const at = `ws://127.0.0.1:${q.address().port}/pair`;

    const host = async (headers) =>
    {
        const h = new Side(at, headers);

        await h.open();
        h.send({ type: 'make' });
        h.paired = await Promise.race([h.next('paired'), h.next('refused')]);
        return h;
    };
    const phone = async (code, hello = { type: 'hello', what: 'a phone' },
                         headers = {}) =>
    {
        const p = new Side(at, headers);

        await p.open();
        p.send({ ...hello, code });
        p.joined = await Promise.race([p.next('joined'), p.next('refused')]);
        return p;
    };

    return { q, host, phone, at };
}

async function paired ()
{
    const { q, host, phone } = await start();

    try
    {
        const h = await host();
        const code = h.paired.code;

        check(h.paired.type === 'paired' && /^[A-Za-z0-9_-]{22}$/.test(code) &&
              typeof h.paired.token === 'string',
              'a page is given a code the relay made, and its own token');

        const p = await phone(code);
        const told = await h.next('peer');

        check(p.joined.type === 'joined' && p.joined.token !== h.paired.token &&
              told?.here === true && told.what === 'a phone',
              'the phone joins with the code, gets a token of its own, and ' +
              'the page is told what it said it is');

        p.send({ type: 'signal', data: { offer: 1 } });
        h.send({ type: 'relayed', data: { frame: [0.5] } });

        const atHost = await h.next('signal');
        const atPhone = await p.next('relayed');

        check(atHost?.data?.offer === 1 && atPhone?.data?.frame?.[0] === 0.5 &&
              atHost.from === undefined,
              'what either side sends reaches the other, unread');

        const second = await phone(code);

        check(second.joined.type === 'refused' && second.joined.why === 'spent',
              'a second phone on a spent code is refused');

        h.send({ type: 'end' });

        const [hEnd, pEnd] = await Promise.all([h.next('ended'), p.next('ended')]);

        const shut = await within(Promise.all([h.closed, p.closed]));

        check(hEnd?.why === 'ended' && pEnd?.why === 'ended' &&
              shut !== 'not in time' && q.pairs.size === 0,
              'the page ending it ends it for both, and the relay forgets it');

        const unknown = await phone(code);

        check(unknown.joined.why === 'unknown', 'an ended code is unknown');
    }
    finally
    {
        q.shutdown();
    }
}

async function raced ()
{
    const { q, host, phone } = await start();

    try
    {
        const h = await host();
        const both = await Promise.all([phone(h.paired.code),
                                        phone(h.paired.code)]);

        check(both.filter((p) => p.joined.type === 'joined').length === 1 &&
              both.filter((p) => p.joined.why === 'spent').length === 1,
              'two phones racing for one code: one joins, one is refused');
    }
    finally
    {
        q.shutdown();
    }
}

async function cameBack ()
{
    const { q, host, phone, at } = await start();

    try
    {
        const h = await host();
        const p = await phone(h.paired.code);

        await h.next('peer');
        await p.close();

        const gone = await h.next('peer');

        check(gone?.here === false, 'the page is told when the phone goes');

        const stranger = await phone(h.paired.code,
                                     { type: 'hello', token: 'nope' });

        check(stranger.joined.why === 'token',
              'a socket without its side\'s token is refused');

        const back = await phone(h.paired.code,
                                 { type: 'hello', token: p.joined.token });
        const here = await h.next('peer');

        await back.next('peer');        /* its own, of the page there */

        check(back.joined.type === 'joined' && here?.here === true,
              'the phone comes back with its token, and the page is told');

        /* The page's socket drops: it comes back on the code, with its own
           token, and the phone hears it gone and back. */
        await h.close();

        const hGone = await back.next('peer');
        const h2 = new Side(at);

        await h2.open();
        h2.send({ type: 'hello', code: h.paired.code, token: h.paired.token });

        const h2joined = await h2.next('joined');
        const hBack = await back.next('peer');

        back.send({ type: 'signal', data: 7 });
        check(hGone?.here === false && h2joined !== null && hBack?.here === true &&
              (await h2.next('signal'))?.data === 7,
              'so does the page, and the two talk again');

        /* A side reopened while its old socket lingers: the new one is the
           side, and the old one is closed. */
        const before = h2.got.filter((m) => m.type === 'peer').length;
        const again = await phone(h.paired.code,
                                  { type: 'hello', token: p.joined.token });

        check(again.joined.type === 'joined' &&
              (await within(back.closed)) === 4001 &&
              await h2.count('peer') === before + 1 &&
              h2.got.filter((m) => m.type === 'peer').at(-1).here === true,
              'a side that comes back again replaces its older socket, and ' +
              'the page is told it is back, though it never saw it go');
    }
    finally
    {
        q.shutdown();
    }
}

async function firstLines ()
{
    const { q, host, at } = await start({ pairs: { helloMs: 200 } });

    try
    {
        const h = await host();
        const garbled = new Side(at);

        await garbled.open();
        garbled.send('not json');
        check((await within(garbled.closed)) !== 'not in time',
              'a first line that is neither make nor hello closes the socket');

        const silent = new Side(at);

        await silent.open();

        const waited = await Promise.race([
            silent.closed.then(() => true),
            new Promise((r) => setTimeout(() => r(false), 2000))]);

        check(waited, 'a socket that says nothing is closed');

        /* A refused hello is the socket's last word: a second, with no
           token, doesn't take the code. */
        const twice = new Side(at);
        const code = h.paired.code;

        await twice.open();
        twice.send({ type: 'hello', code, token: 'x'.repeat(22) });
        twice.send({ type: 'hello', code, what: 'sneaky' });
        await within(twice.closed);

        const after = new Side(at);

        await after.open();
        after.send({ type: 'hello', code, what: 'mine' });
        check((await after.next('joined')) !== null,
              'a socket refused says no more, and the code is still there');

        const elsewhere = new Side(`${at}/${code}`);

        check(await elsewhere.open().then(() => false, () => true),
              'a path under /pair is not upgraded: a code is never in a URL');
    }
    finally
    {
        q.shutdown();
    }
}

/* Whatever a client puts in its lines, the relay goes on: a crash here is
   every room's. */
async function oddLines ()
{
    const { q, host, at } = await start();

    try
    {
        const h = await host();
        const code = h.paired.code;
        const odd = [{ type: 'hello', code, token: { toString: 1 } },
                     { type: 'hello', code, token: { toString: null } },
                     { type: 'hello', code, token: ['a'] },
                     { type: 'hello', code, token: null },
                     { type: 'hello', code, token: 5 },
                     { type: 'hello', code: { toString: 1 } },
                     { type: 'hello', code, what: { length: 1e9 } },
                     { type: 'signal', data: 1 },
                     [], 7, null];
        const whys = [];

        for (const m of odd)
        {
            const s = new Side(at);

            await s.open();
            s.send(JSON.stringify(m));
            whys.push((await s.next('refused', 500))?.why ?? 'closed');
            await within(s.closed);
        }

        const fresh = await host();

        check(fresh.paired.type === 'paired',
              'odd tokens, odd lines and odd things it says it is don\'t take ' +
              'the relay down (' + whys.join(', ') + ')');
        check(whys.slice(0, 5).every((w) => w === 'malformed'),
              'and a token that is not a string is malformed');
    }
    finally
    {
        q.shutdown();
    }
}

async function expired ()
{
    let t = 0;
    const { q, host, phone } = await start({ pairs: { now: () => t } });

    try
    {
        const h = await host();

        t += 2 * 60 * 1000 + 1;

        const late = await phone(h.paired.code);

        check(late.joined.why === 'expired' &&
              (await h.next('ended'))?.why === 'expired' && q.pairs.size === 0,
              'a code nobody joined in two minutes is refused, and the page told');

        const h2 = await host();

        t += 2 * 60 * 1000 + 1;
        q.pairs.sweep();
        check((await h2.next('ended'))?.why === 'expired',
              'and the sweep ends one nobody tried');

        const h3 = await host();
        const p3 = await phone(h3.paired.code);

        await p3.close();
        t += 9 * 60 * 1000;
        q.pairs.sweep();
        check(q.pairs.size === 1, 'a phone gone nine minutes may still come back');

        t += 60 * 1000 + 1;
        q.pairs.sweep();
        check((await h3.next('ended'))?.why === 'gone' && q.pairs.size === 0,
              'gone ten, the pairing is ended');

        const h4 = await host();

        await phone(h4.paired.code);
        t += 24 * 60 * 60 * 1000 + 1;
        q.pairs.sweep();
        check((await h4.next('ended'))?.why === 'old',
              'and any pairing ends after a day');

        /* Each deadline holds before the sweep has come round. */
        const h5 = await host();
        const p5 = await phone(h5.paired.code);

        await p5.close();
        t += 10 * 60 * 1000 + 1;

        const late5 = await phone(h5.paired.code,
                                  { type: 'hello', token: p5.joined.token });

        check(late5.joined.why === 'gone',
              'a phone back after ten minutes is too late, sweep or no sweep');

        const h6 = await host();

        t += 2 * 60 * 1000 + 1;

        const late6 = await phone(h6.paired.code,
                                  { type: 'hello', token: h6.paired.token });

        check(late6.joined.why === 'expired',
              'nor does the page\'s token reopen a code nobody joined in time');

        const h7 = await host();
        const p7 = await phone(h7.paired.code);

        t += 24 * 60 * 60 * 1000 + 1;
        p7.send({ type: 'relayed', data: 1 });
        check((await h7.next('ended'))?.why === 'old' &&
              (await h7.count('relayed')) === 0,
              'and a frame past a day ends the pairing rather than passing');
    }
    finally
    {
        q.shutdown();
    }
}

async function limited ()
{
    const t = 0;
    const { q, host, phone } = await start({
        pairs: { now: () => t, pairsMax: 3, livePerClient: 2,
                 rate: { burst: 5, refillMs: 1000 },
                 limits: [{ burst: 2, refillMs: 1000 }, null, null] } });

    try
    {
        const a = await host();
        const p = await phone(a.paired.code);

        for (let i = 0; i < 20; i++)
            p.send({ type: 'signal', data: i });
        check(await a.count('signal') === 5,
              'past its bucket, what a side sends is dropped');

        p.send({ type: 'signal', data: 'x'.repeat(5000) });
        check(await within(p.closed) === 1009,
              'a frame past the cap closes its socket');

        const b = await host();
        const c = await host();

        check(b.paired.type === 'paired' && c.paired.why === 'busy',
              'one client holds only so many pairings at once');

        /* Coming back is no fresh burst: the phone's bucket is spent
           from the frames above. */
        const rejoin = await phone(a.paired.code,
                                   { type: 'hello', token: p.joined.token });

        check(rejoin.joined.why === 'rate',
              'nor does coming back again and again buy a side more');

        a.send({ type: 'end' });
        await a.closed;

        const d = await host();

        check(d.paired.why === 'rate',
              'and makes new ones only so fast');
    }
    finally
    {
        q.shutdown();
    }

    const full = await start({ pairs: { pairsMax: 1 } });

    try
    {
        const a = await full.host();
        const b = await full.host();

        check(a.paired.type === 'paired' && b.paired.why === 'full',
              'the relay holds only so many pairings');
    }
    finally
    {
        full.q.shutdown();
    }
}

/* A side that stops reading is sent no more than a cap's worth; what the
   other sends past it is dropped, not kept. */
async function backedUp ()
{
    const { q, host, phone } = await start({
        pairs: { rate: { burst: 1e6, refillMs: 1 }, queuedMax: 16 * 1024 } });

    try
    {
        const h = await host();
        const p = await phone(h.paired.code);
        const hostSocket = q.pairs.pairs.get(h.paired.code).host.ws;

        h.ws._socket.pause();
        for (let i = 0; i < 3000; i++)
            p.send({ type: 'relayed', data: 'x'.repeat(3000) });
        await new Promise((r) => setTimeout(r, 500));
        check(hostSocket.bufferedAmount < 64 * 1024,
              'a side that stops reading is queued only so much (' +
              hostSocket.bufferedAmount + ' bytes)');
    }
    finally
    {
        q.shutdown();
    }
}

/* Clients that won't read: one refused that never answers the close, and
   one that pings and never takes the pongs. Each is cut, not kept. */
async function unread ()
{
    const { q, at } = await start({ pairs: { refusedMs: 200, queuedMax: 1000 } });

    try
    {
        const deaf = new Side(at);

        await deaf.open();
        deaf.send({ type: 'hello', code: 'x'.repeat(22) });
        deaf.ws._socket.pause();
        await new Promise((r) => setTimeout(r, 600));
        check(q.pairs.sockets.size === 0,
              'a refused socket that never answers the close is cut');

        const pinger = new Side(at);

        await pinger.open();
        pinger.send({ type: 'make' });
        pinger.ws._socket.pause();

        const payload = Buffer.alloc(125);

        for (let i = 0; i < 200000 && q.pairs.sockets.size > 0; i++)
        {
            pinger.ws.ping(payload);
            if (i % 1000 === 0)
                await new Promise((r) => setImmediate(r));
        }
        await new Promise((r) => setTimeout(r, 300));
        check(q.pairs.sockets.size === 0,
              'and so is one that pings and never reads the pongs');
    }
    finally
    {
        q.shutdown();
    }
}

/* The drop at the cap, exactly: under it a frame passes, at it not. */
async function threshold ()
{
    const { Pairs } = await import('./pair.mjs');
    const pairs = new Pairs({ now: () => 0, queuedMax: 1000 });
    const fake = () => ({ readyState: 1, bufferedAmount: 0, sent: [],
                          send (d) { this.sent.push(d); },
                          on () {}, close () {}, terminate () {} });
    const host = fake();
    const phone = fake();

    pairs.open(host, 'a');
    pairs.open(phone, 'b');

    const p = pairs.make(host, 'a');

    p.joined = true;
    p.phone.ws = phone;
    phone.bufferedAmount = 999;
    pairs.pass(p, 'host', { type: 'relayed', data: 1 });
    phone.bufferedAmount = 1000;
    pairs.pass(p, 'host', { type: 'relayed', data: 2 });
    check(phone.sent.length === 1 && JSON.parse(phone.sent[0]).data === 1,
          'a frame passes with the other side under its cap, and not at it');
}

/* Sockets: only so many from one client, from the upgrade until each
   has gone, whatever it said. */
async function held ()
{
    const { q, host, at } = await start({ pairs: { socketsPerClient: 4 } });

    try
    {
        const h = await host();
        const quiet = [];

        for (let i = 0; i < 3; i++)
        {
            const s = new Side(at);

            await s.open();
            quiet.push(s);
        }

        const more = new Side(at);

        await more.open().catch(() => {});
        check(more.status === 429,
              'one client holds only so many sockets on /pair');

        quiet[0].send({ type: 'hello', code: h.paired.code, what: 'a phone' });
        await quiet[0].next('joined');
        await quiet[1].close();

        const now = new Side(at);

        check(await now.open().then(() => true, () => false),
              'and one gone makes room for another');

        /* The phone may end it too. */
        quiet[0].send({ type: 'end' });
        check((await h.next('ended'))?.why === 'ended',
              'the phone ending it ends it for the page');
    }
    finally
    {
        q.shutdown();
    }
}

async function sited ()
{
    const site = 'https://pages.example.org';
    const { q, host } = await start({ corsOrigin: site });

    try
    {
        const elsewhere = new Side(`ws://127.0.0.1:${q.address().port}/pair`,
                                   { Origin: 'https://evil.example' });

        await elsewhere.open().catch(() => {});
        check(elsewhere.status === 403,
              'a pairing from another site is refused');

        const own = await host({ Origin: site });

        check(own.paired.type === 'paired', 'and one from the site is made');

        const none = new Side(`ws://127.0.0.1:${q.address().port}/pair`);

        await none.open().catch(() => {});
        check(none.status === 403, 'and one from no site at all is refused');
    }
    finally
    {
        q.shutdown();
    }
}

/* A phone's socket cut while it sends: the page is told, and the relay
   goes on for the next. */
async function cut ()
{
    const { q, host, phone } = await start();

    try
    {
        const h = await host();
        const p = await phone(h.paired.code);

        await h.next('peer');
        for (let i = 0; i < 10; i++)
            p.send({ type: 'relayed', data: i });
        p.ws.terminate();

        const gone = await h.next('peer');
        const back = await phone(h.paired.code,
                                 { type: 'hello', token: p.joined.token });

        check(gone?.here === false && back.joined.type === 'joined',
              'a phone cut mid-stream is gone, and can come back');
    }
    finally
    {
        q.shutdown();
    }
}

try
{
    await paired();
    await raced();
    await cameBack();
    await firstLines();
    await oddLines();
    await expired();
    await limited();
    await backedUp();
    await held();
    await threshold();
    await unread();
    await sited();
    await cut();
}
catch (e)
{
    check(false, `threw: ${e.stack}`);
}

process.stdout.write(`\n${failures === 0 ? 'pairing does what it says'
                                          : `${failures} failed`}\n`);
process.exitCode = failures;
