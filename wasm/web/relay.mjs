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
 * relay.mjs -- the room's server: the document, the clock, who is here,
 * and the way peers find each other.
 *
 *   node wasm/web/relay.mjs [--port 8787] [--tree DIR]
 *   node wasm/web/relay.mjs admin <command>      (accounts.mjs, runAdmin)
 *
 * One process, one port. It does three jobs and is
 * authoritative for none of the music: it holds the shared document so a
 * late joiner has somewhere to fetch it from; it answers pings so every
 * peer can agree on one clock; and it says who is in a room, on which
 * seat, and forwards the signalling that lets their browsers open a
 * connection to each other. A note or a knob is never forwarded from here
 * unless the peer-to-peer path failed (section 5.5), and then unread.
 *
 * It does keep the run that is playing, for whoever joins while it plays:
 * the start, the document as the start named it, and a copy of every
 * stamped command since, which each page sends beside its mesh broadcast.
 * A late joiner asks for that once, steps through it from the room's
 * origin, and is playing the room's piece from there (commands.js,
 * catchUp).
 *
 *   GET  /               health: version, accounts, each room's people and
 *                        piece
 *   WS   /doc/<room>     the Yjs document, y-websocket's protocol
 *   WS   /room/<room>    JSON: presence, seats, clock, signalling, chat
 *   /api/account/...     accounts: handles, keys, sessions (accounts.mjs)
 *
 * Two sockets per peer rather than one: y-websocket's framing is its
 * own, and the JSON side is easier to read on the wire and in a harness
 * when it is not sharing a socket with binary CRDT updates.
 *
 * Who someone is, is the room socket's to say: its hello carries an
 * account's session, or nothing for a guest, who goes by a name that is
 * nobody's handle. The document socket cannot say anything first --
 * y-websocket opens it and speaks at once -- so the room socket's welcome
 * hands out a ticket for it, good for one room for a few minutes, and the
 * relay opens no document socket without one.
 *
 * A room is made when the first peer arrives and seeded with a shipped
 * piece -- the .gen, and every .dsp it names, from the tree -- and kept
 * for an hour after the last one leaves. A peer can have it seeded again
 * with another (`switch'). Rooms are not persisted; accounts are, in one
 * SQLite file (DB=..., beside the relay by default).
 */

import crypto from 'node:crypto';
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

import { WebSocketServer } from 'ws';
import * as Y from 'yjs';
import * as awarenessProtocol from 'y-protocols/awareness';
import * as syncProtocol from 'y-protocols/sync';
import * as decoding from 'lib0/decoding';
import * as encoding from 'lib0/encoding';

import { ACCOUNT_API, normalizeName } from './account.js';
import { AccountStore, Accounts, ADMIN_USAGE, accountRoutes,
         runAdmin } from './accounts.mjs';
import { RELAY, TRANSPORT_LEAD } from './commands.js';
import { DEFAULT_PIECE, dspNames, files, hashOfFiles, hasSeen, meta,
         pieceName, putFile, readSeen, seenOf, snapshot } from './doc.js';

export const PROTOCOL = 1;

/* y-websocket's two message types. */
const MSG_SYNC = 0;
const MSG_AWARENESS = 1;

/* How long a start waits for the relay's copy of the document to reach
   the revision it names before keeping what is there. */
const SNAPSHOT_WAIT = 10 * 1000;

/* How long a switch made while the room plays waits for another before
   the relay plays it: switches made together play the last of them once,
   rather than each a Play the next one replaces. */
const SWITCH_GATHER_MS = 50;

/* The most commands a run keeps for late joiners. A knob dragged for an
   hour is well under this; past it, a joiner is told the run cannot be
   caught up with rather than handed part of it. */
const LOG_MAX = 200000;

/* How long an empty room is kept, and how often that is looked at. */
const EMPTY_FOR = 60 * 60 * 1000;
const SWEEP_EVERY = 60 * 1000;

/* Chat: the longest line, and how many a peer may send at once and then
   per second. Enough to talk in, and short of what a stuck key or a
   script would make of a room's screens. */
const CHAT_MAX = 500;
const CHAT_BURST = 5;
const CHAT_PER_SECOND = 5;

/* A document ticket's life. A room socket is handed a new one when two
   fifths of it have gone, so the ticket a page reconnects its document
   with is never one about to lapse. */
const TICKET_MS = 5 * 60 * 1000;

/* How often the sessions behind open room sockets are looked at again:
   the admin commands end sessions from another process, which has no way
   to tell this one. */
const SESSION_CHECK_MS = 60 * 1000;

const here = path.dirname(fileURLToPath(import.meta.url));

/* The relay's clock: milliseconds as a double, from the monotonic clock
   and never from Date.now(), so a step of the system clock does not move
   a room's origin. */
export function relayNow ()
{
    return Number(process.hrtime.bigint()) / 1e6;
}

/* A room's document: a shipped piece and every .dsp it names, from the
   tree, in place of whatever files it had. A piece that is not there
   changes nothing; one that names a .dsp that is not leaves the room with
   what could be read and says so, and the page shows a load error rather
   than the relay refusing the room. */
export function seedFiles (doc, piece, tree)
{
    const genPath = path.join(tree, 'gen', path.basename(piece));
    let gen;

    try
    {
        gen = fs.readFileSync(genPath, 'utf8');
    }
    catch
    {
        process.stderr.write(`relay: no such piece ${genPath}\n`);
        return false;
    }

    doc.transact(() =>
    {
        /* Fresh texts rather than the old ones written over: a keystroke
           still on its way into the piece being replaced lands in a text
           nobody has, not in the middle of the new one. */
        for (const name of [...files(doc).keys()])
            files(doc).delete(name);

        putFile(doc, path.basename(piece), gen);

        for (const name of dspNames(gen))
        {
            try
            {
                putFile(doc, name,
                        fs.readFileSync(path.join(tree, 'dsp', name), 'utf8'));
            }
            catch
            {
                process.stderr.write(`relay: ${piece} names ${name}, which ` +
                                     `is not under ${tree}/dsp\n`);
            }
        }

        meta(doc).set('piece', path.basename(piece));
        meta(doc).set('seeded_from', `gen/${path.basename(piece)}`);
    });

    return true;
}

/* Which run a start began: its sender and counter, which is what a page
   stamps its logged commands with (room.js). */
export function runKeyOf (start)
{
    return start ? `${start.from}#${start.seq}` : null;
}

/* A short random id: for a peer, and for nothing else. */
function newId ()
{
    return Math.random().toString(36).slice(2, 8);
}

/* One room: a document and the peers in it. `accounts' says who a
   session is of, and `tickets' is the relay's, which the document sockets
   are let in by. */
class Room
{
    constructor (name, seedWith, tree, { accounts, tickets, ticketMs })
    {
        this.name = name;
        this.tree = tree;
        this.accounts = accounts;
        this.tickets = tickets;
        this.ticketMs = ticketMs;
        this.doc = new Y.Doc();
        this.awareness = new awarenessProtocol.Awareness(this.doc);
        this.docConns = new Set();          /* document sockets          */
        this.peers = new Map();             /* id -> { ws, name, seat,
                                               account, tickets, docs } */
        this.seats = new Map();             /* seat -> peer id           */
        this.playing = null;                /* the last transport start  */
        this.emptySince = relayNow();

        /* The `switched' lines, in the order the switches were made; how
           many have been made; and the counter of the relay's own Plays,
           as a peer counts its commands. */
        this.switching = Promise.resolve();
        this.switches = 0;
        this.seq = 0;

        /* What a late joiner needs of the run that is playing: its start,
           the document as that start named it, and every stamped command
           since, in arrival order. Null while stopped. */
        this.run = null;

        /* The awareness protocol's own clients: what to forget when a
           socket closes. */
        this.controlled = new Map();        /* ws -> Set of client ids   */

        seedFiles(this.doc, seedWith, tree);

        /* Every update to anyone, as y-websocket does: the document is
           the one thing here that has to reach every peer. */
        this.doc.on('update', (update) =>
        {
            const enc = encoding.createEncoder();

            encoding.writeVarUint(enc, MSG_SYNC);
            syncProtocol.writeUpdate(enc, update);
            this.broadcastDoc(encoding.toUint8Array(enc));
        });

        this.awareness.on('update', ({ added, updated, removed }, origin) =>
        {
            const changed = added.concat(updated, removed);

            if (origin !== null && origin !== undefined &&
                this.controlled.has(origin))
            {
                const ids = this.controlled.get(origin);

                for (const id of added)
                    ids.add(id);

                for (const id of removed)
                    ids.delete(id);
            }

            const enc = encoding.createEncoder();

            encoding.writeVarUint(enc, MSG_AWARENESS);
            encoding.writeVarUint8Array(
                enc, awarenessProtocol.encodeAwarenessUpdate(this.awareness,
                                                             changed));
            this.broadcastDoc(encoding.toUint8Array(enc));
        });
    }

    get empty ()
    {
        return this.peers.size === 0 && this.docConns.size === 0;
    }

    broadcastDoc (bytes)
    {
        for (const ws of this.docConns)
            if (ws.readyState === ws.OPEN)
                ws.send(bytes);
    }

    /* ---- the run ---- */

    begin (start, files = this.snapshotAt(start.piece ?? {}))
    {
        this.playing = start;
        this.run = { start, log: [], overflowed: false, files };
    }

    /* A switch made while the room plays, played: a start of the relay's
       own, from the top, at the revision the switch left, which is the run
       a late joiner is handed. `texts' is that revision, kept when it was
       made, since the document may have moved on by now. The seed is
       drawn here; a piece that pins one plays that instead on every peer
       alike. */
    playSwitch (hash, seen, texts)
    {
        const start = { type: 'transport', at: -1, from: RELAY,
                        seq: this.seq++, op: 'start',
                        origin: relayNow() + TRANSPORT_LEAD * 1000,
                        piece: { hash, seen },
                        seed: Math.floor(Math.random() * 0x100000000),
                        seek: 0 };

        this.begin(start, Promise.resolve({ ...texts, matched: true }));

        const line = JSON.stringify({ type: 'transport', from: RELAY,
                                      data: start });

        for (const p of this.peers.values())
            if (p.ws.readyState === p.ws.OPEN)
                p.ws.send(line);
    }

    /* A stamped command, into the run it was made in. `runKey' is that
       run's start, as the sender knew it (runKeyOf); a copy that arrives
       after another Play has begun is the old run's straggler, stamped for
       a time in a piece that is no longer playing, and is not kept. */
    record (cmd, runKey)
    {
        const { run } = this;

        if (run === null || runKey !== runKeyOf(run.start))
            return;

        if (run.log.length >= LOG_MAX)
            run.overflowed = true;
        else
            run.log.push(cmd);
    }

    /* The document at the revision `hash' names: now, if the relay's copy
       is there, or once an update brings it there. The starter sent its
       edits on the document socket before its Play on this one, but the
       two are separate sockets and nothing orders them. Past the wait,
       what is here, marked as not what was asked for. */
    snapshotAt ({ hash, seen } = {})
    {
        const passed = seen === undefined ? null : readSeen(seen);

        return new Promise((resolve) =>
        {
            const timer = setTimeout(() => finish(snapshot(this.doc), false),
                                     SNAPSHOT_WAIT);
            let checking = false;
            let again = false;
            let done = false;

            const finish = (snap, matched) =>
            {
                if (done)
                    return;

                done = true;
                clearTimeout(timer);
                this.doc.off('update', check);
                resolve({ ...snap, matched });
            };

            const check = async () =>
            {
                if (checking)
                {
                    again = true;
                    return;
                }

                checking = true;

                do
                {
                    again = false;

                    if (done)
                        return;

                    /* The snapshot first and its hash after, so what is
                       handed over is what was hashed: an update landing
                       during the digest would otherwise be in the one and
                       not the other. */
                    const snap = snapshot(this.doc);

                    if (await hashOfFiles(snap.files) === hash)
                    {
                        finish(snap, true);
                        return;
                    }

                    /* Gone past it: no update brings it back. */
                    if (passed !== null && hasSeen(this.doc, passed))
                    {
                        finish(snap, false);
                        return;
                    }
                }
                while (again);

                checking = false;
            };

            this.doc.on('update', check);
            check();
        });
    }

    /* A ticket for `peer''s document socket, which goes when the room
       socket does: whatever opened that and is let in by this should not
       outlast it. */
    issue (peer)
    {
        const now = relayNow();
        const ticket = `t_${crypto.randomBytes(16).toString('hex')}`;

        for (const t of peer.tickets)
            if (!(this.tickets.get(t)?.until > now))
            {
                this.tickets.delete(t);
                peer.tickets.delete(t);
            }

        this.tickets.set(ticket, { room: this, peer,
                                   until: now + this.ticketMs });
        peer.tickets.add(ticket);
        return ticket;
    }

    /* The room sockets whose account `ends' says is over -- logged out,
       banned, deleted -- told why and closed. */
    endSessions (ends, why)
    {
        for (const p of this.peers.values())
            if (p.account !== null && ends(p.account) &&
                p.ws.readyState === p.ws.OPEN)
            {
                p.ws.send(JSON.stringify({ type: 'error', why: 'session',
                                           text: why }));
                p.ws.close();
            }
    }

    /* ---- the document socket ---- */

    attachDoc (ws, owner)
    {
        owner.docs.add(ws);
        this.docConns.add(ws);
        this.controlled.set(ws, new Set());
        ws.binaryType = 'arraybuffer';

        ws.on('message', (data) =>
        {
            let bytes;

            if (data instanceof ArrayBuffer)
                bytes = new Uint8Array(data);
            else if (Array.isArray(data))
                bytes = new Uint8Array(Buffer.concat(data));
            else
                bytes = new Uint8Array(data);

            /* The bytes are untrusted: an empty, truncated or garbage
               frame throws out of the decoder or out of Yjs. One bad
               frame must cost its own socket, not the whole relay. */
            try
            {
                const dec = decoding.createDecoder(bytes);
                const enc = encoding.createEncoder();

                switch (decoding.readVarUint(dec))
                {
                    case MSG_SYNC:
                        encoding.writeVarUint(enc, MSG_SYNC);
                        syncProtocol.readSyncMessage(dec, enc, this.doc, ws);

                        /* A reply only when there is one: step 2 in answer
                           to step 1, or nothing in answer to an update. */
                        if (encoding.length(enc) > 1)
                            ws.send(encoding.toUint8Array(enc));

                        break;

                    case MSG_AWARENESS:
                        awarenessProtocol.applyAwarenessUpdate(
                            this.awareness, decoding.readVarUint8Array(dec),
                            ws);
                        break;
                }
            }
            catch (err)
            {
                process.stderr.write(`relay: bad document frame in room ` +
                                     `${this.name}: ${err.message}\n`);
                ws.close();
            }
        });

        ws.on('close', () =>
        {
            owner.docs.delete(ws);
            this.docConns.delete(ws);
            awarenessProtocol.removeAwarenessStates(
                this.awareness, [...(this.controlled.get(ws) ?? [])], null);
            this.controlled.delete(ws);
            this.touch();
        });

        ws.on('error', () => ws.close());

        /* Sync step 1, and whatever awareness there already is. */
        const enc = encoding.createEncoder();

        encoding.writeVarUint(enc, MSG_SYNC);
        syncProtocol.writeSyncStep1(enc, this.doc);
        ws.send(encoding.toUint8Array(enc));

        const states = this.awareness.getStates();

        if (states.size > 0)
        {
            const aw = encoding.createEncoder();

            encoding.writeVarUint(aw, MSG_AWARENESS);
            encoding.writeVarUint8Array(
                aw, awarenessProtocol.encodeAwarenessUpdate(
                    this.awareness, [...states.keys()]));
            ws.send(encoding.toUint8Array(aw));
        }
    }

    /* ---- the room socket ---- */

    attachRoom (ws)
    {
        let id = null;
        let ticketing = null;
        let chatTokens = CHAT_BURST;
        let chatAt = relayNow();

        const send = (m) =>
        {
            if (ws.readyState === ws.OPEN)
                ws.send(JSON.stringify(m));
        };

        /* To the peers `to' names -- one id, or a list of them -- or,
           with no `to' at all, to everyone in the room but this one.
         *
           Serialised once, however many sockets it goes to: a command to
           a room of eight otherwise costs eight identical stringify calls
           on the one path that is meant to be cheap, because nothing here
           reads what it forwards. An id nobody is on is not an error: a
           peer that left is a peer that left. */
        const toPeers = (m, to) =>
        {
            const s = JSON.stringify(m);
            const put = (p) =>
            {
                if (p !== undefined && p.ws.readyState === p.ws.OPEN)
                    p.ws.send(s);
            };

            if (to === undefined)
            {
                for (const [pid, p] of this.peers)
                    if (pid !== id)
                        put(p);

                return;
            }

            for (const pid of Array.isArray(to) ? to : [to])
                put(this.peers.get(String(pid)));
        };

        const others = (m) => toPeers(m);

        const seatMap = () =>
        {
            const out = {};

            for (const [seat, pid] of this.seats)
                out[seat] = pid;

            return out;
        };

        ws.on('message', (data) =>
        {
            let m;

            try
            {
                m = JSON.parse(data.toString());
            }
            catch
            {
                send({ type: 'error', text: 'not JSON' });
                return;
            }

            if (typeof m !== 'object' || m === null ||
                typeof m.type !== 'string')
                return;

            /* The first message has to be a hello, and only the first. */
            if (id === null)
            {
                if (m.type !== 'hello')
                {
                    send({ type: 'error', text: 'hello first' });
                    return;
                }

                if (m.protocol !== PROTOCOL)
                {
                    send({ type: 'error',
                           text: `protocol ${m.protocol}; this relay ` +
                                 `speaks ${PROTOCOL}` });
                    ws.close();
                    return;
                }

                /* An account plays under its handle. A session the relay
                   no longer knows is refused rather than made a guest, or
                   somebody would play the room believing they were logged
                   in. A guest goes by the name asked for, if that is not
                   an account's. */
                const account = m.session === undefined ? null
                    : this.accounts.sessionAccount({ session: m.session });
                const asked = account?.handle ??
                              normalizeName(String(m.name ?? ''));

                if (m.session !== undefined && account === null)
                {
                    send({ type: 'error', why: 'session',
                           text: 'your session has ended; log in again' });
                    ws.close();
                    return;
                }

                if (account === null && asked !== null &&
                    !this.accounts.nameFree(asked))
                {
                    send({ type: 'error', why: 'name',
                           text: `${asked} is an account's handle; log in, ` +
                                 'or pick another name' });
                    ws.close();
                    return;
                }

                id = newId();

                while (this.peers.has(id) || id === RELAY)
                    id = newId();

                const name = asked ?? id;
                const peer = { ws, name, seat: null, account,
                               tickets: new Set(), docs: new Set() };

                this.peers.set(id, peer);
                ticketing = setInterval(
                    () => send({ type: 'ticket', ticket: this.issue(peer) }),
                    this.ticketMs * 2 / 5);

                send({
                    type: 'welcome',
                    peer: id,
                    identity: { name, account: account !== null },
                    ticket: this.issue(peer),
                    peers: [...this.peers].map(([pid, p]) =>
                        ({ peer: pid, name: p.name, seat: p.seat,
                           account: p.account !== null })),
                    seats: seatMap(),
                    piece: this.doc.getMap('meta').get('piece') ?? null,
                    playing: this.playing,
                    /* What a page cannot assume of an older relay. */
                    features: ['switch'],
                });

                others({ type: 'joined', peer: id, name,
                         account: account !== null });
                return;
            }

            const me = this.peers.get(id);

            switch (m.type)
            {
                /* First claim wins; a taken seat answers with the map as
                   it is and no change. `seat: null' releases. */
                case 'seat':
                {
                    const seat = m.seat === null ? null : Number(m.seat);

                    if (seat !== null &&
                        (!Number.isInteger(seat) || seat < 0 || seat > 15))
                        break;

                    const holder = seat === null ? undefined
                                                 : this.seats.get(seat);

                    if (holder === undefined || holder === id)
                    {
                        if (me.seat !== null)
                            this.seats.delete(me.seat);

                        me.seat = seat;

                        if (seat !== null)
                            this.seats.set(seat, id);
                    }

                    const reply = { type: 'seats', seats: seatMap() };

                    send(reply);
                    others(reply);
                    break;
                }

                case 'ping':
                    send({ type: 'pong', t0: m.t0, t1: relayNow() });
                    break;

                /* Signalling: opaque, and to one peer always -- hence
                   the String, which turns a missing `to' into an id
                   nobody has rather than into the whole room. */
                case 'signal':
                    toPeers({ type: 'signal', from: id, data: m.data },
                            String(m.to));
                    break;

                /* A gesture the mesh could not carry: forwarded unread
                   (section 5.5). `to' is one peer, or the list of peers
                   whose channel is not up -- a page falls back one peer
                   at a time and sends one message for all of them. With
                   no `to' at all it goes to the room. */
                case 'relayed':
                    toPeers({ type: 'relayed', from: id, data: m.data },
                            m.to);
                    break;

                /* A transport start goes here as well as over the mesh:
                   it is the one command a peer must not miss, and the
                   last one is what a joining peer is told (section 5.3).
                   A stop clears it. */
                case 'transport':
                {
                    if (typeof m.data !== 'object' || m.data === null)
                        break;

                    if (m.data.op === 'start' &&
                        m.data.piece?.seen !== undefined &&
                        readSeen(m.data.piece.seen) === null)
                    {
                        send({ type: 'refused', of: 'transport',
                               why: 'not a snapshot' });
                        break;
                    }

                    /* A stop names the run it stops, and one that has been
                       replaced since -- by a switch's Play crossing it --
                       stops nothing here, as on the pages (jam.js). */
                    if (m.data.op === 'start')
                        this.begin(m.data);
                    else if (m.data.op === 'stop')
                    {
                        if (m.data.run === undefined ||
                            m.data.run === runKeyOf(this.playing))
                        {
                            this.playing = null;
                            this.run = null;
                        }
                    }
                    else
                        this.record(m.data, m.run);

                    others({ type: 'transport', from: id, data: m.data });
                    break;
                }

                /* A copy of a stamped command the mesh carried, kept for
                   whoever joins while this run plays. Not forwarded: the
                   others have it. */
                case 'log':
                    if (typeof m.data === 'object' && m.data !== null)
                        this.record(m.data, m.run);
                    break;

                /* A line of text, to everyone in the room and back to its
                   sender, whose copy is how it knows the line went. Who
                   sent it is the relay's to say, and nothing is kept: a
                   peer who arrives later sees what is said after. `bar'
                   is where the sender's transport was, as it read it, and
                   `n' the sender's own count, which only it is told. */
                case 'chat':
                {
                    const text = typeof m.text === 'string' ? m.text.trim()
                                                            : '';
                    const now = relayNow();

                    chatTokens = Math.min(CHAT_BURST, chatTokens +
                                          (now - chatAt) * CHAT_PER_SECOND /
                                          1000);
                    chatAt = now;

                    const n = Number.isSafeInteger(m.n) ? m.n : undefined;

                    /* Format characters alone are a line that shows as
                       nothing, or reorders the lines around it. */
                    const why = m.channel !== 'stage' ? 'no such channel'
                              : text.replace(/\p{Cf}/gu, '').trim() === ''
                                  ? 'nothing to send'
                              : text.length > CHAT_MAX
                                  ? `longer than ${CHAT_MAX} characters`
                              : chatTokens < 1 ? 'too fast; wait a moment'
                              : null;

                    if (why !== null)
                    {
                        send({ type: 'refused', of: 'chat', why, n });
                        break;
                    }

                    chatTokens--;

                    const line = { type: 'chat', channel: m.channel, from: id,
                                   name: me.name,
                                   account: me.account !== null, text };

                    if (typeof m.bar === 'string' &&
                        /^\d{1,6}\.\d{1,2}$/.test(m.bar))
                        line.bar = m.bar;

                    send({ ...line, n });
                    others(line);
                    break;
                }

                /* Another shipped piece for the room, at one peer's word.
                   Here and not on the pages, so that two switches at once
                   are one after the other rather than two peers' writes
                   merged into a document holding both pieces. Everyone is
                   told who switched, and the revision the document is at
                   after it. While the room plays, the relay plays the last
                   of the switches made together (playSwitch). */
                case 'switch':
                {
                    const piece = typeof m.piece === 'string' ? m.piece : '';

                    if (piece !== path.basename(piece) ||
                        !piece.endsWith('.gen') ||
                        !seedFiles(this.doc, piece, this.tree))
                    {
                        send({ type: 'refused', of: 'switch',
                               why: 'no such piece', piece });
                        break;
                    }

                    const texts = snapshot(this.doc);
                    const seen = seenOf(this.doc);
                    const made = ++this.switches;

                    /* Played only if the run it was made in is still the
                       room's after the gathering: a Play, a seek or a Stop
                       in between is somebody's answer to it already. */
                    const playingThen = this.playing;

                    this.switching = this.switching
                        .then(() => hashOfFiles(texts.files))
                        .then(async (hash) =>
                        {
                            const line = { type: 'switched', from: id,
                                           name: me.name,
                                           account: me.account !== null,
                                           piece, hash };

                            send(line);
                            others(line);

                            if (playingThen === null)
                                return;

                            await new Promise((r) =>
                                setTimeout(r, SWITCH_GATHER_MS));

                            if (this.playing === playingThen &&
                                made === this.switches)
                                this.playSwitch(hash, seen, texts);
                        });
                    break;
                }

                /* A late joiner, ready to play: the run as the relay has
                   it, or `start: null' when nothing is playing. A Play
                   while the snapshot is awaited makes the answer that
                   run, which is what the joiner should hear. */
                case 'catchup':
                {
                    const answer = () =>
                    {
                        const run = this.run;

                        if (run === null)
                        {
                            send({ type: 'catchup', start: null });
                            return;
                        }

                        run.files.then((files) =>
                        {
                            if (this.run !== run)
                                answer();
                            else
                                send({ type: 'catchup', start: run.start,
                                       files, log: run.log,
                                       overflowed: run.overflowed });
                        });
                    };

                    answer();
                    break;
                }
            }
        });

        ws.on('close', () =>
        {
            clearInterval(ticketing);

            if (id === null)
                return;

            const me = this.peers.get(id);

            for (const t of me.tickets)
                this.tickets.delete(t);

            for (const d of me.docs)
                d.close();

            if (me?.seat !== null && me?.seat !== undefined)
                this.seats.delete(me.seat);

            this.peers.delete(id);
            others({ type: 'left', peer: id });
            others({ type: 'seats', seats: seatMap() });
            this.touch();
        });

        ws.on('error', () => ws.close());
    }

    touch ()
    {
        if (this.empty)
            this.emptySince = relayNow();
    }

    /* Every socket cut and every timer stopped: the awareness keeps one
       of its own, to age out states, and a room that only dropped its
       document would keep the process alive by it. */
    destroy ()
    {
        for (const ws of this.docConns)
            ws.terminate();

        for (const p of this.peers.values())
            p.ws.terminate();

        this.awareness.destroy();
        this.doc.destroy();
    }
}

/* The server. Resolves with it listening; `address().port' says where.
   `db' is the accounts' file, or ':memory:'; `corsOrigin' and
   `trustProxy' are accountRoutes'. The two times are for a harness. */
export function relay ({ port = 8787, host = '0.0.0.0',
                         tree = path.join(here, '..', '..'), db = ':memory:',
                         corsOrigin = null, trustProxy = 0,
                         ticketMs = TICKET_MS,
                         sessionCheckMs = SESSION_CHECK_MS } = {})
{
    const rooms = new Map();
    const tickets = new Map();          /* ticket -> { room, peer, until } */
    const store = new AccountStore(db);

    /* Sessions ended over HTTP: one, or all of an account's but one. */
    const accounts = new Accounts({
        store,
        onSessionsEnded: (ended, why) =>
        {
            for (const r of rooms.values())
                r.endSessions(ended.session !== undefined
                    ? (a) => a.sessionHash === ended.session
                    : (a) => a.id === ended.account &&
                             a.sessionHash !== ended.except, why);
        },
    });
    const api = accountRoutes(accounts, { corsOrigin, trustProxy });

    const room = (name, seedWith) =>
    {
        let r = rooms.get(name);

        if (r === undefined)
        {
            r = new Room(name, seedWith, tree,
                         { accounts, tickets, ticketMs });
            rooms.set(name, r);
        }

        return r;
    };

    const server = http.createServer((req, res) =>
    {
        const url = new URL(req.url, 'http://localhost');

        if (url.pathname.startsWith(`${ACCOUNT_API}/`))
        {
            api(req, res);
            return;
        }

        if (url.pathname === '/')
        {
            res.writeHead(200, { 'Content-Type': 'application/json',
                                 'Access-Control-Allow-Origin': '*' });
            res.end(JSON.stringify({
                thinksynth: 'relay', protocol: PROTOCOL, accounts: true,
                rooms: [...rooms].map(([name, r]) =>
                    ({ name, peers: r.peers.size, piece: pieceName(r.doc),
                       playing: r.playing !== null })),
            }) + '\n');
            return;
        }

        res.writeHead(404).end();
    });

    const wss = new WebSocketServer({ noServer: true });

    server.on('upgrade', (req, socket, head) =>
    {
        const url = new URL(req.url, 'http://localhost');
        const m = /^\/(doc|room)\/([A-Za-z0-9_.-]{1,64})$/.exec(url.pathname);

        if (m === null)
        {
            socket.destroy();
            return;
        }

        /* The piece a new room is seeded with is named in the query,
           and only counts for the first socket to reach the room. */
        const seedWith = url.searchParams.get('piece') ?? DEFAULT_PIECE;
        const given = url.searchParams.get('ticket');
        const ticket = m[1] === 'doc' ? tickets.get(given) : null;

        if (m[1] === 'doc' && !(ticket?.room === rooms.get(m[2]) &&
                                ticket.until > relayNow()))
        {
            socket.end('HTTP/1.1 403 Forbidden\r\n' +
                       'Connection: close\r\n\r\n');
            return;
        }

        wss.handleUpgrade(req, socket, head, (ws) =>
        {
            /* The room socket may have gone while this one upgraded. */
            if (m[1] === 'doc' && !tickets.has(given))
                ws.close();
            else if (m[1] === 'doc')
                ticket.room.attachDoc(ws, ticket.peer);
            else
                room(m[2], seedWith).attachRoom(ws);
        });
    });

    /* Sessions ended by the admin commands, which another process ran. */
    const recheck = setInterval(() =>
    {
        for (const r of rooms.values())
            r.endSessions((a) => accounts.sessionAccount(a) === null,
                          'your session has ended; log in again');
    }, sessionCheckMs);

    recheck.unref();

    /* Empty rooms go after an hour, and lapsed tickets with them. */
    const sweep = setInterval(() =>
    {
        for (const [t, { until }] of tickets)
            if (until <= relayNow())
                tickets.delete(t);

        for (const [name, r] of rooms)
            if (r.empty && relayNow() - r.emptySince > EMPTY_FOR)
            {
                r.destroy();
                rooms.delete(name);
            }
    }, SWEEP_EVERY);

    sweep.unref();

    /* Down, now: every socket cut, since close() alone waits for them
       and an upgraded socket is nobody's to wait for. */
    server.shutdown = () =>
    {
        clearInterval(sweep);
        clearInterval(recheck);

        for (const r of rooms.values())
            r.destroy();

        rooms.clear();
        store.close();
        server.closeAllConnections?.();
        server.close();
    };

    return new Promise((resolve) =>
        server.listen(port, host, () =>
        {
            server.rooms = rooms;
            server.accounts = accounts;
            resolve(server);
        }));
}

if (process.argv[1] !== undefined &&
    import.meta.url === pathToFileURL(process.argv[1]).href)
{
    const args = process.argv.slice(2);

    /* DB names the accounts' file; CORS_ORIGIN the page's origin, when it
       is served from somewhere else; TRUST_PROXY how many proxies in front
       append to X-Forwarded-For (1 behind nginx alone). */
    const opts = { db: process.env.DB || path.join(here, 'relay.db'),
                   corsOrigin: process.env.CORS_ORIGIN || null,
                   trustProxy: Number(process.env.TRUST_PROXY ?? 0) };

    if (args[0] === 'admin')
    {
        if (opts.db === ':memory:' || !fs.existsSync(opts.db))
        {
            process.stderr.write(`relay.mjs: no accounts at ${opts.db}; set ` +
                                 `DB to the relay's file\n${ADMIN_USAGE}\n`);
            process.exit(2);
        }

        const store = new AccountStore(opts.db);
        const status = runAdmin(args.slice(1), store,
                                (line) => process.stdout.write(`${line}\n`));

        store.close();
        process.exit(status);
    }

    if (!(Number.isInteger(opts.trustProxy) && opts.trustProxy >= 0))
    {
        process.stderr.write('relay.mjs: TRUST_PROXY is a count of proxies\n');
        process.exit(2);
    }

    for (let i = 0; i < args.length; i++)
    {
        if (args[i] === '--port' && i + 1 < args.length)
            opts.port = parseInt(args[++i], 10);
        else if (args[i] === '--host' && i + 1 < args.length)
            opts.host = args[++i];
        else if (args[i] === '--tree' && i + 1 < args.length)
            opts.tree = args[++i];
        else
        {
            process.stderr.write(
                'usage: relay.mjs [--port N] [--host ADDR] [--tree DIR]\n' +
                '       relay.mjs admin <command>\n');
            process.exit(2);
        }
    }

    if (opts.tree !== undefined && !fs.existsSync(path.join(opts.tree, 'gen')))
    {
        process.stderr.write(`relay.mjs: no gen/ under ${opts.tree}\n`);
        process.exit(2);
    }

    const server = await relay(opts);
    const a = server.address();

    process.stdout.write(`relay on ws://${a.address}:${a.port}/  ` +
                         `(rooms seeded from ${path.resolve(
                             opts.tree ?? path.join(here, '..', '..'))}, ` +
                         `accounts in ${opts.db})\n`);
}
