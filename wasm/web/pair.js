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
 * pair.js -- a phone and the page it plays, as a room of two
 * (docs/PHONE.md).
 *
 * The relay's /pair socket (relay/pair.mjs) in the shape mesh.js wants of
 * room.js -- `peer', `peers', `on', `signal(to, data)', `relayed(data,
 * to)' -- so the two pages' WebRTC link, its fall back through the relay
 * and its retries are the room's own.
 *
 *   new Pair(url, { make: true })                 the page: a new pairing
 *   new Pair(url, { code, token, host: true })    the page, back
 *   new Pair(url, { code, token })                the phone, back
 *   new Pair(url, { code, what })                 the phone, the first time
 *
 * The mesh has 'joined', 'left', 'signal' and 'relayed'. The page has
 * the rest: 'paired' (code, token), the code to show and the token to
 * keep; 'token' (token), the phone's to keep; 'peer' (here, what), the
 * other side come or gone; and 'ended' (why) or 'refused' (why), after
 * which there is nothing to come back to.
 *
 * The page is `host' and the phone `phone', so the page offers. A dropped
 * socket comes back by itself with its token. And whenever the two find
 * each other again, the mesh is told the other left and joined: neither
 * can know whether the other's page still holds the connection it had, so
 * both start one afresh. Which connection a signal is for is the page's
 * to say, since it offers: it names each it begins (its epoch), and every
 * signal either side sends carries the page's epoch. The page drops one
 * for any other; the phone takes a new one as the page beginning afresh,
 * begins afresh too, and drops one for an epoch it has left. The phone
 * never begins one of its own, so the two can't go on making each other
 * begin again.
 *
 * A socket the relay closed because the same side came back elsewhere, in
 * another tab, say, is that side's no longer, and doesn't come back: the
 * two would take the pairing from each other for good.
 */

const BACK_FIRST_MS = 500;
const BACK_MAX_MS = 10000;

const newEpoch = () => Math.random().toString(36).slice(2);

/* relay/pair.mjs's close for a socket its side has replaced. */
const REPLACED = 4001;

/* The site's relay's /pair: the build's config.json, as the room page
   finds its relay (jam.js), or the page's own host on the relay's usual
   port, over TLS for a page that is: an https page can't open ws:. A
   phone uses its own site's, never one a link names, so a link can't send
   its player's touches elsewhere. */
export async function pairUrl ()
{
    let relay = null;

    try
    {
        relay = (await (await fetch('config.json')).json()).relay;
    }
    catch
    {
    }
    const scheme = location.protocol === 'https:' ? 'wss' : 'ws';

    return `${(relay || `${scheme}://${location.hostname}:8787`).replace(/\/+$/, '')}/pair`;
}

export class Pair
{
    constructor (url, { make = false, host = make, code = null, token = null,
                        what = '' } = {})
    {
        this.url = url;
        this.code = code;
        this.token = token;
        this.what = what;
        this.peer = host ? 'host' : 'phone';
        this.other = host ? 'phone' : 'host';
        this.peers = new Map();
        this.handlers = new Map();
        this.over = false;
        this.tries = 0;
        this.ws = null;
        /* The connection the two are on: the page's own; the phone's the
           page's, once it has heard one. */
        this.epoch = host ? newEpoch() : null;
        this.last = null;               /* the phone's before it began afresh */
        this.left = new Set();          /* epochs the phone has left */
        this.connect(make);
    }

    on (type, fn)
    {
        this.handlers.set(type, fn);
        return this;
    }

    emit (type, ...args)
    {
        this.handlers.get(type)?.(...args);
    }

    connect (make)
    {
        const ws = new WebSocket(this.url);

        this.ws = ws;
        ws.addEventListener('open', () =>
        {
            ws.send(JSON.stringify(make
                ? { type: 'make' }
                : { type: 'hello', code: this.code, what: this.what,
                    ...(this.token === null ? {} : { token: this.token }) }));
        });
        ws.addEventListener('message', (e) =>
        {
            let m;

            try
            {
                m = JSON.parse(e.data);
            }
            catch
            {
                return;
            }
            if (ws === this.ws)
                this.heard(m);
        });
        ws.addEventListener('close', (e) =>
        {
            if (ws !== this.ws || this.over)
                return;
            if (e.code === REPLACED)
            {
                this.finish('ended', 'opened elsewhere');
                return;
            }
            /* A socket that never got as far as a token has nothing to
               come back with: a phone whose first socket went with the
               relay's answer to it has a code that is spent. */
            if (this.code === null || this.token === null)
            {
                this.finish('refused', this.code === null
                    ? 'the relay could not be reached'
                    : 'lost before the pairing was made; scan again');
                return;
            }
            this.back();
        });
    }

    back ()
    {
        setTimeout(() =>
        {
            if (!this.over)
                this.connect(false);
        }, Math.min(BACK_FIRST_MS * 2 ** this.tries++, BACK_MAX_MS));
    }

    /* The mesh's link to the other side made afresh, and this side's
       connection with it. */
    relink ()
    {
        if (this.peers.has(this.other))
            this.emit('left', this.other);
        if (this.peer === 'host')
            this.epoch = newEpoch();
        else
        {
            /* Whatever the page sends next, on this epoch or a new one:
               the page may not have begun afresh too. */
            this.last = this.epoch ?? this.last;
            this.epoch = null;
        }
        this.peers.set(this.other, {});
        this.emit('joined', this.other);
    }

    heard (m)
    {
        if (m.type === 'paired')
        {
            this.code = m.code;
            this.token = m.token;
            this.tries = 0;
            this.emit('paired', m.code, m.token);
        }
        else if (m.type === 'joined')
        {
            this.token = m.token;
            this.tries = 0;
            this.emit('token', m.token);
        }
        else if (m.type === 'peer')
        {
            if (m.here)
                this.relink();
            this.emit('peer', m.here, m.what);
        }
        else if (m.type === 'signal' && typeof m.data?.epoch === 'string')
        {
            const epoch = m.data.epoch;

            if (this.peer === 'host' ? epoch !== this.epoch : this.left.has(epoch))
                return;
            /* The page has begun a connection this side wasn't told of:
               begin afresh here too, on its epoch. */
            if (this.peer === 'phone' && epoch !== this.epoch)
            {
                if (this.epoch !== null)
                    this.relink();
                /* Left only once the page is on another. */
                if (this.last !== null && this.last !== epoch)
                    this.left.add(this.last);
                this.last = null;
                this.epoch = epoch;
            }
            this.emit('signal', this.other, m.data.data);
        }
        else if (m.type === 'relayed')
            this.emit('relayed', this.other, m.data);
        /* Coming back too soon: the socket's close brings it back. */
        else if (m.type === 'refused' && m.why === 'rate' && this.token !== null)
            return;
        else if (m.type === 'refused' && this.token !== null)
            /* Refused coming back: the pairing ended while this side was
               away, or the relay has forgotten it. */
            this.finish('ended', m.why);
        /* By name here, not by what the message says it is: what a socket
           sends never names the handler it reaches. */
        else if (m.type === 'ended')
            this.finish('ended', m.why);
        else if (m.type === 'refused')
            this.finish('refused', m.why);
    }

    finish (type, why)
    {
        if (this.over)
            return;
        this.over = true;
        if (this.peers.delete(this.other))
            this.emit('left', this.other);
        this.ws?.close();
        this.emit(type, why);
    }

    send (m)
    {
        if (this.ws?.readyState === 1)
            this.ws.send(JSON.stringify(m));
    }

    signal (to, data)
    {
        this.send({ type: 'signal', data: { epoch: this.epoch, data } });
    }

    /* `lossy': a frame the next makes good, so not one to queue behind
       another the socket hasn't sent yet. */
    relayed (data, to, lossy = false)
    {
        if (!(lossy && this.ws?.bufferedAmount > 0))
            this.send({ type: 'relayed', data });
    }

    /* For both sides: the pairing over. While this side's socket is down
       the other isn't told, and the relay ends it once this side has been
       gone long enough. */
    end ()
    {
        this.send({ type: 'end' });
        this.finish('ended', 'ended');
    }
}
