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
 * mesh.js -- the gesture path between peers (JAM_M3.md, section 5).
 *
 * One RTCPeerConnection per pair, the peer with the smaller id offering,
 * offer, answer and ICE candidates through the room socket's `signal'.
 * On it one data channel, unordered and without retransmission: a knob
 * that arrives late is worse than one that does not arrive, since a later
 * knob has superseded it, and a note that arrives late is just late.
 *
 * And the way round it. If a pair's channel has not opened within
 * OPEN_WITHIN, or drops, both sides send each other their gestures
 * through the relay instead: the same commands, one more hop. The page
 * shows which peers are which, with the round trip to each, so a LAN with
 * awkward ICE still gives a tape to compare and a failure is visible
 * rather than mysterious.
 */

/* STUN, for a candidate the other side can reach. TURN is M5's. */
const ICE = { iceServers: [{ urls: 'stun:stun.l.google.com:19302' }] };

/* How long a pair gets to open its channel before the relay carries it. */
const OPEN_WITHIN = 10000;

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

    /* What the page shows: for each peer, 'direct', 'relayed' or
       'connecting', and the round trip in milliseconds if known. */
    status (peer)
    {
        const l = this.links.get(peer);

        if (l === undefined)
            return { path: 'none', rtt: NaN };

        return { path: l.relayed ? 'relayed'
                       : l.channel?.readyState === 'open' ? 'direct'
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

        const pc = new RTCPeerConnection(ICE);
        const l = { pc, channel: null, relayed: false, rtt: NaN,
                    pinger: null, timer: null, pending: [] };

        this.links.set(peer, l);

        pc.addEventListener('icecandidate', (e) =>
        {
            if (e.candidate !== null)
                this.room.signal(peer, { candidate: e.candidate.toJSON() });
        });

        pc.addEventListener('connectionstatechange', () =>
        {
            if (pc.connectionState === 'failed' ||
                pc.connectionState === 'disconnected' ||
                pc.connectionState === 'closed')
                this.fallBack(peer, `the connection ${pc.connectionState}`);
        });

        const offering = this.room.peer < peer;

        if (offering)
        {
            this.attach(peer, l, pc.createDataChannel('gestures', {
                ordered: false, maxRetransmits: 0 }));

            pc.createOffer()
                .then((offer) => pc.setLocalDescription(offer))
                .then(() => this.room.signal(
                    peer, { description: pc.localDescription.toJSON() }))
                .catch((e) => this.fallBack(peer, `no offer: ${e.message}`));
        }
        else
            pc.addEventListener('datachannel', (e) =>
                this.attach(peer, l, e.channel));

        /* The clock on opening. */
        l.timer = setTimeout(() =>
        {
            if (l.channel?.readyState !== 'open')
                this.fallBack(peer, `not open in ${OPEN_WITHIN / 1000} s`);
        }, OPEN_WITHIN);
    }

    attach (peer, l, channel)
    {
        l.channel = channel;

        channel.addEventListener('open', () =>
        {
            clearTimeout(l.timer);
            l.pinger = setInterval(() =>
            {
                if (channel.readyState === 'open')
                    channel.send(JSON.stringify({ ping: performance.now() }));
            }, PING_EVERY);
            this.emit('change', peer);
        });

        channel.addEventListener('close', () =>
            this.fallBack(peer, 'the channel closed'));

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

    /* Offer, answer or candidate from the other side. */
    async signalled (from, data)
    {
        let l = this.links.get(from);

        if (l === undefined)
        {
            this.link(from);
            l = this.links.get(from);
        }

        const pc = l?.pc;

        if (pc === undefined)
            return;

        try
        {
            if (data.description !== undefined)
            {
                await pc.setRemoteDescription(data.description);

                for (const c of l.pending.splice(0))
                    await pc.addIceCandidate(c);

                if (data.description.type === 'offer')
                {
                    await pc.setLocalDescription(await pc.createAnswer());
                    this.room.signal(
                        from, { description: pc.localDescription.toJSON() });
                }
            }
            else if (data.candidate !== undefined)
            {
                /* A candidate before the description is held, since
                   addIceCandidate wants the description first. */
                if (pc.remoteDescription === null)
                    l.pending.push(data.candidate);
                else
                    await pc.addIceCandidate(data.candidate);
            }
        }
        catch (e)
        {
            this.fallBack(from, `signalling: ${e.message}`);
        }
    }

    fallBack (peer, why)
    {
        const l = this.links.get(peer);

        if (l === undefined || l.relayed)
            return;

        l.relayed = true;
        l.rtt = NaN;
        clearTimeout(l.timer);
        clearInterval(l.pinger);
        l.pc?.close();
        this.emit('fallback', peer, why);
        this.emit('change', peer);
    }

    drop (peer)
    {
        const l = this.links.get(peer);

        if (l === undefined)
            return;

        clearTimeout(l.timer);
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

        for (const [peer, l] of this.links)
        {
            if (!l.relayed && l.channel?.readyState === 'open')
                l.channel.send(text);
            else
                this.room.relayed(cmd, peer);
        }
    }

    close ()
    {
        for (const peer of [...this.links.keys()])
            this.drop(peer);
    }
}
