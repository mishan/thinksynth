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
 * mesh.js -- the gesture path between peers.
 *
 * One RTCPeerConnection per pair, the peer with the smaller id offering,
 * offer, answer and ICE candidates through the room socket's `signal'.
 * On it two data channels. Everything goes on one unordered and without
 * retransmission: a knob that arrives late is worse than one that does not
 * arrive, since a later knob has superseded it. Everything but knobs goes
 * on one ordered and reliable as well: a lost key is a note left sounding,
 * a lost mute or param two peers composing two pieces from there, and
 * either is worse than late. The first copy to arrive is applied and the
 * other dropped (commands.js, Dedupe), so the reliable channel's wait for
 * a retransmission holds up only what the other channel lost too.
 *
 * A page from before the second channel offers only the first, and takes
 * the second as another of the first: it gets everything either way, and
 * sends everything on the one it has.
 *
 * And the way round it. If a pair's channels have not opened within
 * OPEN_WITHIN, or the connection drops, both sides send each other their
 * gestures through the relay instead: the same commands, one more hop.
 * The connection is not given up on: it goes on trying, and is made again
 * if it gets nowhere, and the pair is direct again once it opens. The page
 * shows which peers are which, with the round trip to each, so a LAN with
 * awkward ICE still gives a tape to compare and a failure is visible
 * rather than mysterious.
 */

/* STUN, for a candidate the other side can reach. TURN is for later. */
const ICE = { iceServers: [{ urls: 'stun:stun.l.google.com:19302' }] };

/* How long a pair gets to open its channel before the relay carries it. */
const OPEN_WITHIN = 10000;

/* Once relayed, how long until the offering side tries the pair again,
   doubling to the last. */
const RETRY_FIRST_MS = 5000;
const RETRY_MAX_MS = 60000;

/* A ping over the channel this often, for the round trip shown. */
const PING_EVERY = 1000;

export class Mesh
{
    /* `room' is room.js's; `onCommand(from, cmd)' gets every command that
       arrives, by either path. */
    constructor (room, onCommand)
    {
        this.room = room;
        this.onCommand = onCommand;
        this.links = new Map();         /* peer id -> link */
        this.handlers = new Map();

        room.on('signal', (from, data) => this.signalled(from, data));
        room.on('relayed', (from, data) => this.received(from, data));
        room.on('joined', (peer) => this.link(peer));
        room.on('left', (peer) => this.drop(peer));

        for (const peer of room.peers.keys())
            if (peer !== room.peer)
                this.link(peer);
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

    /* Both channels open, or the one a page from before the second
       offered. */
    open (l)
    {
        return l.channel?.readyState === 'open' &&
               (l.keys === null ? !l.keyed : l.keys.readyState === 'open');
    }

    /* What the page shows: for each peer, 'direct', 'relayed' or
       'connecting', and the round trip in milliseconds if known. */
    status (peer)
    {
        const l = this.links.get(peer);

        if (l === undefined)
            return { path: 'none', rtt: NaN };

        return { path: l.relayed ? 'relayed'
                       : this.open(l) ? 'direct'
                       : 'connecting',
                 rtt: l.rtt };
    }

    /* A link to one peer, offered by whichever of the two sorts first. */
    link (peer)
    {
        if (this.links.has(peer) || typeof RTCPeerConnection === 'undefined')
        {
            if (!this.links.has(peer))
                this.links.set(peer, { relayed: true, rtt: NaN });

            return;
        }

        const l = { relayed: false, rtt: NaN, pinger: null, timer: null,
                    retry: null, tries: 0 };

        this.links.set(peer, l);
        this.connect(peer, l, 0);
    }

    /* Connection `gen' of a pair: the first, or one the offering side
       made again after the last would not open or came apart. Signals
       carry it, so a late one for a connection gone is not taken for its
       successor's. */
    connect (peer, l, gen)
    {
        l.pc?.close();
        clearTimeout(l.timer);
        clearInterval(l.pinger);

        const pc = new RTCPeerConnection(ICE);
        const offering = this.room.peer < peer;

        Object.assign(l, { pc, gen, channel: null, keys: null, pending: [] });

        pc.addEventListener('icecandidate', (e) =>
        {
            if (e.candidate !== null)
                this.room.signal(peer, { gen,
                                         candidate: e.candidate.toJSON() });
        });

        /* Disconnected is a network that may come back, and ICE finds
           its way back by itself: relayed meanwhile, direct again once it
           has. Failed is one an ICE restart may find again, which keeps
           the channels and what is queued on them. */
        pc.addEventListener('connectionstatechange', () =>
        {
            if (l.pc !== pc)
                return;

            if (pc.connectionState === 'connected')
                this.back(peer, l);
            else if (pc.connectionState === 'disconnected' ||
                     pc.connectionState === 'failed')
                this.fallBack(peer, `the connection ${pc.connectionState}`);

            if (pc.connectionState !== 'failed')
                return;

            if (offering)
                pc.restartIce();
            else
                this.room.signal(peer, { gen, restart: true });
        });

        if (offering)
        {
            /* The reliable one first: the other side is given it first,
               so it is open by the time the unreliable one is. */
            this.attach(peer, l, pc.createDataChannel('keys'));
            this.attach(peer, l, pc.createDataChannel('gestures', {
                ordered: false, maxRetransmits: 0 }));

            /* The first offer, and an ICE restart's. */
            pc.addEventListener('negotiationneeded', () =>
                pc.createOffer()
                    .then((offer) => pc.setLocalDescription(offer))
                    .then(() => this.room.signal(
                        peer, { gen,
                                description: pc.localDescription.toJSON() }))
                    .catch((e) =>
                    {
                        if (l.pc === pc)
                            this.fallBack(peer, `no offer: ${e.message}`);
                    }));
        }
        else
            pc.addEventListener('datachannel', (e) =>
                this.attach(peer, l, e.channel));

        /* The clock on opening. */
        l.timer = setTimeout(() =>
        {
            if (!this.open(l))
                this.fallBack(peer, `not open in ${OPEN_WITHIN / 1000} s`);
        }, OPEN_WITHIN);
    }

    attach (peer, l, channel)
    {
        if (channel.label === 'keys')
            l.keys = channel;
        else
            l.channel = channel;

        const current = () => channel === l.channel || channel === l.keys;

        channel.addEventListener('open', () => this.back(peer, l));

        channel.addEventListener('close', () =>
        {
            if (current())
                this.fallBack(peer, 'the channel closed');
        });

        channel.addEventListener('message', (e) =>
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

            if (m.ping !== undefined)
            {
                if (channel.readyState === 'open')
                    channel.send(JSON.stringify({ pong: m.ping }));
            }
            else if (m.pong !== undefined)
            {
                l.rtt = performance.now() - m.pong;
                this.emit('change', peer);
            }
            else
                this.received(peer, m);
        });
    }

    /* The pair direct, for the first time or again: both channels open,
       and the connection through any disconnect it had. */
    back (peer, l)
    {
        if (!this.open(l) || l.pc.connectionState === 'disconnected' ||
            l.pc.connectionState === 'failed')
            return;

        clearTimeout(l.timer);
        clearTimeout(l.retry);
        clearInterval(l.pinger);
        l.retry = null;
        l.tries = 0;
        l.relayed = false;
        l.pinger = setInterval(() =>
        {
            if (l.channel.readyState === 'open')
                l.channel.send(JSON.stringify({ ping: performance.now() }));
        }, PING_EVERY);
        this.emit('change', peer);
    }

    /* Offer, answer, candidate or restart from the other side. */
    async signalled (from, data)
    {
        let l = this.links.get(from);

        if (l === undefined)
        {
            this.link(from);
            l = this.links.get(from);
        }

        if (l?.pc === undefined)
            return;

        /* A page from before generations sends none: what it sends is
           for the one connection it has, whichever this side's is. */
        const gen = data.gen ?? l.gen;

        /* The offering side has made the pair again: this side's
           connection goes, and one comes to answer it. */
        if (gen > l.gen && data.description?.type === 'offer')
            this.connect(from, l, gen);

        if (gen !== l.gen)
            return;

        const { pc, pending } = l;

        try
        {
            if (data.restart === true)
                pc.restartIce();
            else if (data.description !== undefined)
            {
                /* A page with the second channel sends a generation,
                   and is not direct until both have opened. */
                if (data.description.type === 'offer')
                    l.keyed = data.gen !== undefined;

                await pc.setRemoteDescription(data.description);

                for (const c of pending.splice(0))
                    await pc.addIceCandidate(c);

                if (data.description.type === 'offer')
                {
                    await pc.setLocalDescription(await pc.createAnswer());
                    this.room.signal(
                        from, { gen,
                                description: pc.localDescription.toJSON() });
                }
            }
            else if (data.candidate !== undefined)
            {
                /* A candidate before the description is held, since
                   addIceCandidate wants the description first. */
                if (pc.remoteDescription === null)
                    pending.push(data.candidate);
                else
                    await pc.addIceCandidate(data.candidate);
            }
        }
        catch (e)
        {
            if (l.pc === pc)
                this.fallBack(from, `signalling: ${e.message}`);
        }
    }

    /* The relay carries the pair's gestures until it is direct again. */
    fallBack (peer, why)
    {
        const l = this.links.get(peer);

        if (l === undefined)
            return;

        this.later(peer, l);

        if (l.relayed)
            return;

        l.relayed = true;
        l.rtt = NaN;
        clearTimeout(l.timer);
        clearInterval(l.pinger);
        this.emit('fallback', peer, why);
        this.emit('change', peer);
    }

    /* A relayed pair tried again, backing off: direct again if what
       relayed it changed nothing, and on the offering side a new
       connection in place of one that has not got anywhere or has come
       apart. One still connecting, or finding its way back from a
       disconnect or a failure, is left to it: an ICE restart keeps the
       channels and what is queued on them, and a new connection throws
       that away. */
    later (peer, l)
    {
        if (l.pc === undefined || l.retry !== null)
            return;

        l.retry = setTimeout(() =>
        {
            l.retry = null;

            if (this.links.get(peer) !== l)
                return;

            this.back(peer, l);

            if (!l.relayed)
                return;

            const gone = (c) => c?.readyState === 'closing' ||
                                c?.readyState === 'closed';

            if (this.room.peer < peer &&
                (['new', 'closed'].includes(l.pc.connectionState) ||
                 gone(l.channel) || gone(l.keys)))
                this.connect(peer, l, l.gen + 1);

            this.later(peer, l);
        }, Math.min(RETRY_FIRST_MS * 2 ** l.tries++, RETRY_MAX_MS));
    }

    drop (peer)
    {
        const l = this.links.get(peer);

        if (l === undefined)
            return;

        clearTimeout(l.timer);
        clearTimeout(l.retry);
        clearInterval(l.pinger);
        l.pc?.close();
        this.links.delete(peer);
        this.emit('change', peer);
    }

    received (from, cmd)
    {
        this.onCommand(from, cmd);
    }

    /* A command to every other peer, by whichever path each has. */
    broadcast (cmd)
    {
        const text = JSON.stringify(cmd);
        const through = [];         /* whose channel is not carrying it */

        for (const [peer, l] of this.links)
        {
            if (!l.relayed && this.open(l))
            {
                l.channel.send(text);

                if (cmd.type !== 'knob' && l.keys !== null)
                    l.keys.send(text);
            }
            else
                through.push(peer);
        }

        /* One message naming all of them, rather than one each: the
           relay serialises it once and sends it on. */
        if (through.length > 0)
            this.room.relayed(cmd, through);
    }

    close ()
    {
        for (const peer of [...this.links.keys()])
            this.drop(peer);
    }
}
