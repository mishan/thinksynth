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
 * room.js -- the page's side of the relay's room socket: hello, who is
 * here, seats, the clock, signalling, and the relayed path for gestures
 * the mesh could not carry.
 *
 * One object, events out, a few calls in. It knows nothing about music:
 * `transport' and `relayed' carry whatever they are given.
 */

import { RelayClock } from './clock.js';

export const PROTOCOL = 1;

/* How often the relay is pinged, in milliseconds. Once a second is what a
   background tab is throttled to anyway. */
const PING_EVERY = 1000;

export class Room
{
    /* `url' is the relay, ws://host:port; `name' is what the others see.
       `now' is the wall clock the offset is kept against -- the page's
       performance.now, or a harness's. */
    constructor (url, roomName, name,
                 { now = () => performance.now(), piece = null } = {})
    {
        this.url = url;
        this.roomName = roomName;
        this.name = name;
        this.now = now;
        this.piece = piece;             /* what a new room is seeded with */
        this.peer = null;               /* our id, from the welcome */
        this.peers = new Map();         /* id -> { name, seat } */
        this.playing = null;            /* the last transport start */
        this.clock = new RelayClock();
        this.handlers = new Map();
        this.pinger = null;
        this.ws = null;
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
                                     (this.piece ? `?piece=${this.piece}`
                                                 : ''));
            let welcomed = false;
            let refused = null;     /* the relay's last word, if it said one */

            this.ws = ws;

            ws.addEventListener('open', () =>
                this.send({ type: 'hello', name: this.name,
                            protocol: PROTOCOL }));

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
                    reject(new Error(
                        refused ?? `the relay at ${this.url} closed the ` +
                                   'connection before welcoming us'));

                this.emit('close');
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
                        this.peers.clear();

                        for (const p of m.peers)
                            this.peers.set(p.peer, { name: p.name,
                                                     seat: p.seat });

                        this.playing = m.playing;
                        this.pinger = setInterval(() => this.ping(),
                                                  PING_EVERY);
                        this.ping();
                        resolve(m);
                        this.emit('peers');
                        break;

                    case 'joined':
                        this.peers.set(m.peer, { name: m.name, seat: null });
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

                    case 'error':
                        refused = m.text;
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

    /* A transport command, kept by the relay for whoever arrives next. */
    transport (data)
    {
        this.send({ type: 'transport', data });
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
