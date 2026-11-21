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
 * pair.mjs -- a phone and the page it plays, introduced (docs/PHONE.md).
 *
 *   WS /pair, whose first line is
 *     { type: 'make' }                  the page's: answered
 *                                       { type: 'paired', code, token }
 *     { type: 'hello', code, token?, what? }
 *                                       either side's, with its token
 *                                       unless it is the phone's first
 *                                       time: answered { type: 'joined',
 *                                       token }
 *
 * Then what either sends, { type: 'signal' | 'relayed', data }, reaches
 * the other unread; { type: 'end' } from either ends the pairing for
 * both; and each side is told { type: 'peer', here, what } as the other
 * comes and goes. A refusal is { type: 'refused', why } and the socket
 * closed.
 *
 * Not a room: a pairing has no document, seats or listing, and holds
 * exactly two. The code is spent once the phone has joined, so a link
 * seen afterwards plays nothing; a side that drops comes back with the
 * token it was given. The code and the tokens are in first lines, never
 * in a URL, which proxies log. Nothing is kept on disk.
 */

import crypto from 'node:crypto';

import { Bucket, TieredLimit } from './accounts.mjs';

export const PAIR = {
    /* A code nobody has joined: long enough to scan, short enough that
       one shown on a stream or a projector is soon nothing. */
    codeMs: 2 * 60 * 1000,
    /* A side gone this long ends the pairing; and any ends after a day,
       so a pairing whose two sides never leave can't hold a place for
       good. */
    goneMs: 10 * 60 * 1000,
    lifeMs: 24 * 60 * 60 * 1000,
    /* The first line, which a socket that never sends is closed for; how
       many sockets one client may hold, from the upgrade until they have
       gone, refused ones too; and how long a refused one has to go. */
    helloMs: 5000,
    socketsPerClient: 32,
    refusedMs: 1000,
    /* A phone's frame is a few hundred bytes; signalling a few KiB. */
    frameMax: 4096,
    /* About 60 frames a second, twice over for both channels' copies,
       for each side across all its sockets. A rejoin costs one too. */
    rate: { burst: 240, refillMs: 1000 / 160 },
    /* What may wait for a side that is slow to read: past it, frames are
       dropped, since a late frame is worth nothing to a phone's player. */
    queuedMax: 64 * 1024,
    pairsMax: 4096,
    livePerClient: 8,
    /* New pairings, per address and per IPv6 /48. */
    limits: [{ burst: 10, refillMs: 6000 },
             { burst: 40, refillMs: 1500 },
             null],
};

/* What a phone may say it is, shown on the page it pairs with. */
const WHAT_MAX = 64;

function newSecret ()
{
    return crypto.randomBytes(16).toString('base64url');
}

/* Equal without saying, by how long it took, how much was. Both strings. */
function sameSecret (a, b)
{
    const x = Buffer.from(a);
    const y = Buffer.from(b);

    return x.length === y.length && crypto.timingSafeEqual(x, y);
}

const other = (side) => (side === 'host' ? 'phone' : 'host');

export class Pairs
{
    /* `now' is the relay's clock in ms; the rest is PAIR's, for a harness. */
    constructor ({ now, ...given } = {})
    {
        this.now = now;
        this.o = { ...PAIR, ...given };
        this.pairs = new Map();             /* code -> pairing */
        this.live = new Map();              /* client -> pairings made */
        this.held = new Map();              /* client -> sockets open */
        this.sockets = new Set();
        this.limit = new TieredLimit(this.o.limits, now, () => {});
    }

    get size ()
    {
        return this.pairs.size;
    }

    /* Whether `client' may open another socket on /pair. */
    mayOpen (client)
    {
        return (this.held.get(client) ?? 0) < this.o.socketsPerClient;
    }

    send (ws, m)
    {
        if (ws?.readyState === 1)
            ws.send(JSON.stringify(m));
    }

    /* Told why, and given a moment to go: a client that never answers
       the close is cut rather than holding its place. */
    refuse (ws, why)
    {
        this.send(ws, { type: 'refused', why });
        ws.close();
        setTimeout(() => ws.terminate(), this.o.refusedMs).unref();
    }

    /* A socket on /pair, from `client'. */
    open (ws, client)
    {
        let pairing = null, side = null, helloed = false;
        const hello = setTimeout(() => ws.terminate(), this.o.helloMs);

        hello.unref();
        this.sockets.add(ws);
        this.held.set(client, (this.held.get(client) ?? 0) + 1);

        /* A frame past the cap, or a reset, is an error on the socket;
           unheard, it would take the whole relay down. */
        ws.on('error', () => ws.terminate());
        ws.on('close', () =>
        {
            const n = this.held.get(client) - 1;

            if (n > 0)
                this.held.set(client, n);
            else
                this.held.delete(client);
            clearTimeout(hello);
            this.sockets.delete(ws);
            if (side !== null && pairing[side].ws === ws)
                this.leave(pairing, side);
        });
        /* ws answers a ping by itself, past any queue's cap: a client that
           pings and never reads is cut instead. */
        ws.on('ping', () =>
        {
            if (ws.bufferedAmount >= this.o.queuedMax * 4)
                ws.terminate();
        });

        ws.on('message', (data) =>
        {
            /* Whatever a client sends, nothing it sends may throw out of
               here: that would be the whole process. */
            try
            {
                let m = null;

                try
                {
                    m = JSON.parse(data);
                }
                catch
                {
                }

                if (side === null)
                {
                    /* One first line a socket: a refused one says no more. */
                    if (helloed)
                        return;
                    helloed = true;
                    clearTimeout(hello);
                    if (m?.type === 'make')
                    {
                        pairing = this.make(ws, client);
                        side = pairing === null ? null : 'host';
                    }
                    else if (m?.type === 'hello')
                        [pairing, side] = this.join(ws, m) ?? [null, null];
                    else
                        ws.close();
                    return;
                }

                if (pairing[side].ws !== ws)
                    return;
                if (m?.type === 'end')
                    this.end(pairing, 'ended');
                else if (m?.type === 'signal' || m?.type === 'relayed')
                    this.pass(pairing, side, { type: m.type, data: m.data });
            }
            catch (e)
            {
                process.stderr.write(`relay: pair: ${e.stack}\n`);
                ws.terminate();
            }
        });
    }

    /* To the other side, unless this one is past its bucket or the other
       is too far behind to want it. */
    pass (pairing, side, m)
    {
        const to = pairing[other(side)].ws;
        const why = this.due(pairing);

        if (why !== null)
            this.end(pairing, why);
        else if (pairing[side].bucket.take() &&
                 (to?.bufferedAmount ?? 0) < this.o.queuedMax)
            this.send(to, m);
    }

    /* Why a pairing is over by now, or null: its code unjoined too long,
       a side gone too long, or a day old. One test for a join, a frame
       and the sweep, so none of them can bring back one that is over. */
    due (p)
    {
        const t = this.now();

        if (!p.joined && t - p.made > this.o.codeMs)
            return 'expired';
        if (t - p.made > this.o.lifeMs)
            return 'old';
        if ([p.host, p.phone].some((s) => s.ws === null && s.goneAt !== null &&
                                    t - s.goneAt > this.o.goneMs))
            return 'gone';
        return null;
    }

    make (ws, client)
    {
        const mine = this.live.get(client) ?? 0;

        if (this.pairs.size >= this.o.pairsMax)
        {
            this.refuse(ws, 'full');
            return null;
        }
        if (mine >= this.o.livePerClient)
        {
            this.refuse(ws, 'busy');
            return null;
        }
        try
        {
            this.limit.take(client);
        }
        catch
        {
            this.refuse(ws, 'rate');
            return null;
        }

        const side = (token) => ({ ws: null, token, goneAt: null, what: '',
                                   bucket: new Bucket(this.o.rate, this.now) });
        const pairing = { code: newSecret(), client, made: this.now(),
                          joined: false,
                          host: side(newSecret()), phone: side(null) };

        pairing.host.ws = ws;
        this.pairs.set(pairing.code, pairing);
        this.live.set(client, mine + 1);
        this.send(ws, { type: 'paired', code: pairing.code,
                        token: pairing.host.token });
        return pairing;
    }

    /* A hello: the phone's first, or either side back. [pairing, side],
       or nothing once refused. */
    join (ws, m)
    {
        const pairing = typeof m.code === 'string'
            ? this.pairs.get(m.code) : undefined;
        const due = pairing === undefined ? null : this.due(pairing);
        const why = (() =>
        {
            if (pairing === undefined)
                return 'unknown';
            if (m.token !== undefined && typeof m.token !== 'string')
                return 'malformed';
            if (due !== null)
                return due;
            if (m.token === undefined && pairing.joined)
                return 'spent';
            if (m.token !== undefined &&
                !sameSecret(m.token, pairing.host.token) &&
                !(pairing.joined && sameSecret(m.token, pairing.phone.token)))
                return 'token';
            return null;
        })();

        if (due !== null)
            this.end(pairing, due);
        if (why !== null)
        {
            this.refuse(ws, why);
            return null;
        }

        let side;

        if (m.token === undefined)
        {
            side = 'phone';
            pairing.joined = true;
            pairing.phone.token = newSecret();
            pairing.phone.what = typeof m.what === 'string'
                ? m.what.slice(0, WHAT_MAX) : '';
        }
        else
            side = sameSecret(m.token, pairing.host.token) ? 'host' : 'phone';

        const own = pairing[side];
        const them = pairing[other(side)];

        /* Coming back is not a way round the bucket. */
        if (m.token !== undefined && !own.bucket.take())
        {
            this.refuse(ws, 'rate');
            return null;
        }

        /* One socket a side: the one that came back is the one that is. */
        const was = own.ws;

        own.ws = ws;
        own.goneAt = null;
        was?.close();

        /* The other side is told on every return, a socket replaced too:
           this side may have lost what the old one had, and the two start
           their connection afresh on hearing it. */
        this.send(ws, { type: 'joined', token: own.token });
        this.send(ws, { type: 'peer', here: them.ws !== null, what: them.what });
        this.send(them.ws, { type: 'peer', here: true, what: own.what });
        return [pairing, side];
    }

    leave (pairing, side)
    {
        const own = pairing[side];

        own.ws = null;
        own.goneAt = this.now();
        this.send(pairing[other(side)].ws,
                  { type: 'peer', here: false, what: own.what });
    }

    end (pairing, why)
    {
        if (!this.pairs.delete(pairing.code))
            return;

        const n = this.live.get(pairing.client) - 1;

        if (n > 0)
            this.live.set(pairing.client, n);
        else
            this.live.delete(pairing.client);

        for (const s of [pairing.host, pairing.phone])
        {
            const ws = s.ws;

            s.ws = null;
            this.send(ws, { type: 'ended', why });
            ws?.close();
        }
    }

    /* Codes nobody joined in time, pairings a side left too long, and
       any a day old. */
    sweep ()
    {
        for (const p of [...this.pairs.values()])
        {
            const why = this.due(p);

            if (why !== null)
                this.end(p, why);
        }
    }

    /* Down, now: every pairing ended and every socket cut, a hello's
       sockets with them. */
    close ()
    {
        for (const p of [...this.pairs.values()])
            this.end(p, 'closed');
        for (const ws of this.sockets)
            ws.terminate();
    }
}
