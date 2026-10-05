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
 * room.js -- the page's side of the relay's room socket: hello, who is
 * here, seats, the clock, signalling, chat, and the relayed path for
 * gestures the mesh could not carry.
 *
 * One object, events out, a few calls in. It knows nothing about music:
 * `transport', `relayed', `log' and `catchup' carry whatever they are
 * given.
 */

import { shownName } from './account.js';
import { RelayClock } from './clock.js';

export const PROTOCOL = 1;

/* How often the relay is pinged, in milliseconds. Once a second is what a
   background tab is throttled to anyway. */
const PING_EVERY = 1000;

/* How long a catchup is waited for: past the relay's own wait for the
   document (relay.mjs, SNAPSHOT_WAIT), so an answer that is coming has
   come. A relay from before `catchup' never answers. */
const CATCHUP_WAIT = 15 * 1000;

export class Room
{
    /* `url' is the relay, ws://host:port; `name' is what the others see
       of a guest, and `session' an account's, which plays under its
       handle instead. `now' is the wall clock the offset is kept against
       -- the page's performance.now, or a harness's. `was' is the last
       ticket of the room socket this one replaces, whose peer the relay
       then lets go. */
    constructor (url, roomName, name,
                 { now = () => performance.now(), piece = null,
                   session = null, was = null } = {})
    {
        this.url = url;
        this.roomName = roomName;
        this.name = name;
        this.session = session;
        this.was = was;
        this.now = now;
        this.piece = piece;             /* what a new room is seeded with,
                                           '' nothing */
        this.peer = null;               /* our id, from the welcome */
        this.identity = null;           /* { name, account }, likewise */
        this.ticket = null;             /* the document socket's way in */

        /* id -> { name, seat, account }, the name as the room shows it
           (account.js, shownName). */
        this.peers = new Map();
        this.playing = null;            /* the last transport start */
        this.clock = new RelayClock();
        this.handlers = new Map();
        this.pinger = null;
        this.ws = null;
        this.catchups = [];             /* { resolve, reject } of catchUp() */
        this.features = [];             /* what the relay says it does */
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

    /* Resolves with the welcome once the relay has answered the hello. */
    connect ()
    {
        return new Promise((resolve, reject) =>
        {
            const ws = new WebSocket(`${this.url}/room/${this.roomName}` +
                                     (this.piece !== null
                                          ? `?piece=${this.piece}` : ''));
            let welcomed = false;

            /* The relay's last word, if it said one: { text, why }. */
            let refused = null;

            this.ws = ws;

            ws.addEventListener('open', () =>
                this.send({ type: 'hello', name: this.name,
                            protocol: PROTOCOL, tickets: true,
                            ...(this.session === null
                                ? {} : { session: this.session }),
                            ...(this.was === null
                                ? {} : { was: this.was }) }));

            ws.addEventListener('error', () =>
                reject(new Error(`could not reach the relay at ${this.url}`)));

            ws.addEventListener('close', () =>
            {
                clearInterval(this.pinger);
                this.pinger = null;

                /* A relay that turns the hello down -- a protocol
                   mismatch, say -- sends an error and closes, and a
                   clean close fires no error event. Without this the
                   join would await a promise that never settles. */
                if (!welcomed)
                    reject(Object.assign(new Error(
                        refused?.text ?? `the relay at ${this.url} closed ` +
                                         'the connection before welcoming us'),
                        { why: refused?.why }));

                for (const c of this.catchups.splice(0))
                    c.reject(new Error('the relay closed the connection'));

                this.emit('close', refused);
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

                switch (m.type)
                {
                    case 'welcome':
                        welcomed = true;
                        this.peer = m.peer;
                        this.identity = m.identity ?? { name: this.name };
                        this.ticket = m.ticket ?? null;
                        this.peers.clear();

                        for (const p of m.peers)
                            this.peers.set(p.peer, { name: shownName(p),
                                                     seat: p.seat,
                                                     account: p.account });

                        this.playing = m.playing;
                        this.features = m.features ?? [];
                        this.pinger = setInterval(() => this.ping(),
                                                  PING_EVERY);
                        this.ping();
                        resolve(m);
                        this.emit('peers');
                        break;

                    case 'joined':
                        this.peers.set(m.peer, { name: shownName(m),
                                                 seat: null,
                                                 account: m.account });
                        this.emit('peers');
                        this.emit('joined', m.peer);
                        break;

                    case 'left':
                        this.peers.delete(m.peer);
                        this.emit('peers');
                        this.emit('left', m.peer);
                        break;

                    case 'seats':
                        /* The map is the relay's word on who sits
                           where; it is kept on the peers and nowhere
                           else, so there is one answer to ask. */
                        for (const p of this.peers.values())
                            p.seat = null;

                        for (const [seat, pid] of Object.entries(m.seats))
                        {
                            const p = this.peers.get(pid);

                            if (p !== undefined)
                                p.seat = Number(seat);
                        }

                        this.emit('peers');
                        break;

                    case 'pong':
                        this.clock.sample(m.t0, m.t1, this.now());
                        this.emit('clock');
                        break;

                    case 'signal':
                        this.emit('signal', m.from, m.data);
                        break;

                    case 'relayed':
                        this.emit('relayed', m.from, m.data);
                        break;

                    case 'transport':
                        if (m.data?.op === 'start')
                            this.playing = m.data;
                        else if (m.data?.op === 'stop')
                            this.playing = null;

                        this.emit('transport', m.from, m.data);
                        break;

                    case 'chat':
                        this.emit('chat', { ...m, name: shownName(m) });
                        break;

                    /* A new ticket before the last one lapses, for the
                       document socket's next reconnect. */
                    case 'ticket':
                        this.ticket = m.ticket;
                        this.emit('ticket', m.ticket);
                        break;

                    case 'refused':
                        this.emit('refused', m);
                        break;

                    case 'switched':
                        this.emit('switched', { ...m, name: shownName(m) });
                        break;

                    case 'catchup':
                        for (const c of this.catchups.splice(0))
                            c.resolve(m);
                        break;

                    case 'error':
                        refused = { text: m.text, why: m.why };
                        this.emit('error', m.text);
                        break;
                }
            });
        });
    }

    send (m)
    {
        if (this.ws?.readyState === WebSocket.OPEN)
            this.ws.send(JSON.stringify(m));
    }

    ping ()
    {
        this.send({ type: 'ping', t0: this.now() });
    }

    /* Our seat, from the last map the relay sent. */
    get seat ()
    {
        return this.peers.get(this.peer)?.seat ?? null;
    }

    claim (seat)
    {
        this.send({ type: 'seat', seat });
    }

    signal (to, data)
    {
        this.send({ type: 'signal', to, data });
    }

    /* A gesture through the relay: to one peer, to the several named in
       an array, or -- with no `to' -- to everyone else. */
    relayed (data, to)
    {
        this.send(to === undefined ? { type: 'relayed', data }
                                   : { type: 'relayed', to, data });
    }

    /* Another shipped piece for the room, written by the relay. A relay
       without `switch' in its features ignores this. */
    switchPiece (piece)
    {
        this.send({ type: 'switch', piece });
    }

    /* The run this page believes is playing, as the relay keys it: the
       start's sender and counter (relay.mjs, runKeyOf). */
    get runKey ()
    {
        return this.playing ? `${this.playing.from}#${this.playing.seq}`
                            : null;
    }

    /* A transport command, kept by the relay for whoever arrives next. A
       start begins a run and a stop ends it; this page's own are what it
       knows first. */
    transport (data)
    {
        if (data?.op === 'start')
            this.playing = data;
        else if (data?.op === 'stop')
            this.playing = null;

        this.send({ type: 'transport', data, run: this.runKey });
    }

    /* A copy of a stamped command the mesh carried, for the relay to keep
       for whoever joins while this run plays. */
    log (data)
    {
        this.send({ type: 'log', data, run: this.runKey });
    }

    /* Line `n' of our chat. The relay says who sent it and sends it back
       to us as well, with `n'; `bar' is where our transport is, or null
       while stopped. False if there is no connection to send it on. */
    chat (text, n, bar = null)
    {
        if (this.ws?.readyState !== WebSocket.OPEN)
            return false;

        this.send(bar === null ? { type: 'chat', channel: 'stage', text, n }
                               : { type: 'chat', channel: 'stage', text, n,
                                   bar });
        return true;
    }

    /* What a peer joining a playing room needs: resolves to `{ start,
       files, log, overflowed }' -- the run's start, the document as that
       start named it, and the stamped commands since -- or `{ start: null
       }' when nothing is playing. Rejects if the relay closes or has not
       answered in `wait' milliseconds. */
    catchUp (wait = CATCHUP_WAIT)
    {
        return new Promise((resolve, reject) =>
        {
            const c = {
                resolve: (m) => { clearTimeout(timer); resolve(m); },
                reject: (e) => { clearTimeout(timer); reject(e); },
            };
            const timer = setTimeout(() =>
            {
                this.catchups.splice(this.catchups.indexOf(c), 1);
                reject(new Error('the relay did not answer a catchup'));
            }, wait);

            this.catchups.push(c);
            this.send({ type: 'catchup' });
        });
    }

    /* Relay time now, from the offset: NaN until a pong has come. */
    relayNow ()
    {
        return this.clock.relayOf(this.now());
    }

    close ()
    {
        this.ws?.close();
    }
}
