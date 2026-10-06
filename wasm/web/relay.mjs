#!/usr/bin/env node
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
 *   /api/account/...     accounts: handles, keys, sessions (accounts.mjs),
 *                        and passkeys (passkeys.mjs)
 *
 * and, with METRICS_PORT set, on that port on 127.0.0.1 alone,
 *
 *   GET  /               what the relay has carried and how it is keeping
 *                        up, for a load test (relayload.mjs)
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
import { monitorEventLoopDelay } from 'node:perf_hooks';

import { WebSocketServer } from 'ws';
import * as Y from 'yjs';
import * as awarenessProtocol from 'y-protocols/awareness';
import * as syncProtocol from 'y-protocols/sync';
import * as decoding from 'lib0/decoding';
import * as encoding from 'lib0/encoding';

import { ACCOUNT_API, isOrigin, normalizeName, shownName } from './account.js';
import { AccountStore, Accounts, ADMIN_USAGE, accountRoutes,
         runAdmin } from './accounts.mjs';
import { Passkeys, passkeyConfig } from './passkeys.mjs';
import { RELAY, TRANSPORT_LEAD } from './commands.js';
import { DEFAULT_PIECE, dspNames, files, hashOfFiles, hasSeen, meta,
         pieceName, putFile, readSeen, seenOf, snapshot } from './doc.js';

export const PROTOCOL = 1;

/* y-websocket's two message types. */
const MSG_SYNC = 0;
const MSG_AWARENESS = 1;

/* A cursor color as editor.js's colourOf writes it, with or without the
   selection's alpha. */
const CURSOR_COLOR = /^hsl\(\d{1,3} 70% 45%( \/ 0\.25)?\)$/;

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

/* And the most it keeps in bytes, of JSON, and of one command: an edit
   carries a piece's texts, and nothing a page sends is longer than
   that. */
const LOG_BYTES_MAX = 32 * 1024 * 1024;
const LOG_ENTRY_MAX = 512 * 1024;

/* Awareness clients one document socket may speak for, and a room may
   hold: a page is one, and a reconnect briefly has the old socket's
   too. */
const CLIENTS_PER_SOCKET = 2;
const CLIENTS_PER_ROOM = 256;

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

/* How long the relay waits on the accounts' file locked by another
   process -- the admin commands, a backup -- before it fails the request.
   The wait is on the event loop, and every room stops for it. */
const STORE_BUSY_MS = 100;

/* The largest frame a socket may send: a room socket's are JSON lines,
   the longest a start carrying its snapshot (doc.js, SEEN_MAX); a
   document socket's are Yjs updates, the largest a whole piece pasted or
   a first sync of everything a page holds. */
const ROOM_FRAME_MAX = 1024 * 1024;
const DOC_FRAME_MAX = 4 * 1024 * 1024;

/* Document sockets one room socket may have open at once: a page has
   one, and a reconnect briefly two. */
const DOCS_PER_PEER = 4;

/* How often every socket is pinged; one that has not answered the last
   ping by the next is cut. A document socket says nothing while nobody
   types, and one whose page went away without a close would otherwise
   hold its cursor in the room for good. */
const HEARTBEAT_MS = 30 * 1000;

/* How often the sessions behind open room sockets are looked at again:
   the admin commands end sessions from another process, which has no way
   to tell this one. */
const SESSION_CHECK_MS = 60 * 1000;

/* How often the metrics port's event-loop timers run. Node's histogram
   holds the whole time between its runs, this included, so what is
   reported is the time past it. */
const DELAY_RESOLUTION_MS = 10;

/* The room socket's message types, each counted apart for the metrics
   port. Any other a client sends is counted as `other', so that a made-up
   type costs one counter and not one more each. */
const ROOM_TYPES = ['hello', 'welcome', 'joined', 'left', 'seat', 'seats',
                    'ping', 'pong', 'signal', 'relayed', 'transport', 'log',
                    'chat', 'switch', 'switched', 'catchup', 'ticket',
                    'refused', 'error', 'other'];

const here = path.dirname(fileURLToPath(import.meta.url));

/* What the relay has carried since it started, for the metrics port:
   messages and bytes each way, by room-message type and for document
   frames, and the catch-ups answered. Integers only, added to on the path
   every message takes; an outgoing line is measured from the string the
   relay sends anyway. Without the port there is none, and the path
   measures nothing. */
class Traffic
{
    constructor ()
    {
        const counts = () => ({ in: 0, inBytes: 0, out: 0, outBytes: 0 });

        this.room = new Map(ROOM_TYPES.map((t) => [t, counts()]));
        this.doc = counts();
        this.catchups = 0;
        this.catchupMaxBytes = 0;
    }

    roomIn (type, bytes)
    {
        const c = this.room.get(type) ?? this.room.get('other');

        c.in++;
        c.inBytes += bytes;
    }

    /* `line' went to `n' sockets; its length in bytes. */
    roomOut (type, line, n = 1)
    {
        const c = this.room.get(type) ?? this.room.get('other');
        const bytes = Buffer.byteLength(line);

        c.out += n;
        c.outBytes += bytes * n;
        return bytes;
    }

    docIn (bytes)
    {
        this.doc.in++;
        this.doc.inBytes += bytes.length;
    }

    docOut (bytes, n = 1)
    {
        this.doc.out += n;
        this.doc.outBytes += bytes.length * n;
    }

    /* 0 bytes: the asker had gone. */
    catchup (bytes)
    {
        if (bytes === 0)
            return;

        this.catchups++;
        this.catchupMaxBytes = Math.max(this.catchupMaxBytes, bytes);
    }
}

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
    constructor (name, seedWith, tree,
                 { accounts, tickets, ticketMs, sessions, traffic })
    {
        this.name = name;
        this.tree = tree;
        this.accounts = accounts;
        this.tickets = tickets;
        this.ticketMs = ticketMs;
        this.sessions = sessions;           /* whether a hello's counts  */
        this.traffic = traffic;
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
        this.clientSocket = new Map();      /* client id -> its ws       */
        this.docOwner = new Map();          /* ws -> its room socket's peer */
        this.leaving = new Map();           /* room ws -> its leave()    */

        if (seedWith !== '')
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
                for (const id of added)
                {
                    this.controlled.get(origin).add(id);
                    this.clientSocket.set(id, origin);
                }

            /* Gone whoever said so: the socket, its close, or the
               awareness timing a quiet client out. */
            for (const id of removed)
            {
                this.controlled.get(this.clientSocket.get(id))?.delete(id);
                this.clientSocket.delete(id);
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
        let n = 0;

        for (const ws of this.docConns)
            if (ws.readyState === ws.OPEN)
            {
                ws.send(bytes);
                n++;
            }

        this.traffic?.docOut(bytes, n);
    }

    sendDoc (ws, bytes)
    {
        ws.send(bytes);
        this.traffic?.docOut(bytes);
    }

    /* ---- the run ---- */

    begin (start, files = this.snapshotAt(start.piece ?? {}))
    {
        this.playing = start;
        this.run = { start, log: [], bytes: 0, overflowed: false, files };
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
        let n = 0;

        for (const p of this.peers.values())
            if (p.ws.readyState === p.ws.OPEN)
            {
                p.ws.send(line);
                n++;
            }

        this.traffic?.roomOut('transport', line, n);
    }

    /* A stamped command, into the run it was made in. `runKey' is that
       run's start, as the sender knew it (runKeyOf); a copy that arrives
       after another Play has begun is the old run's straggler, stamped for
       a time in a piece that is no longer playing, and is not kept.
       `bytes' is the size of the frame it came in, which holds it: what
       it counts against the caps, without stringifying it again on the
       path every command takes. */
    record (cmd, runKey, bytes)
    {
        const { run } = this;

        if (run === null || runKey !== runKeyOf(run.start))
            return;

        if (run.log.length >= LOG_MAX || bytes > LOG_ENTRY_MAX ||
            run.bytes + bytes > LOG_BYTES_MAX)
            run.overflowed = true;
        else
        {
            run.log.push(cmd);
            run.bytes += bytes;
        }
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
       banned, deleted -- told why and closed. Their tickets and document
       sockets go now rather than when the close completes, which a
       client that has stopped answering can hold off for half a
       minute. */
    endSessions (ends, why)
    {
        for (const p of this.peers.values())
            if (p.account !== null && ends(p.account) &&
                p.ws.readyState === p.ws.OPEN)
            {
                const line = JSON.stringify({ type: 'error', why: 'session',
                                              text: why });

                p.ws.send(line);
                this.traffic?.roomOut('error', line);
                p.ws.close();
                this.leaving.get(p.ws)();
            }
    }

    /* Whether a peer is an account, as the room is told: null on a relay
       without accounts, where nobody is a guest for not being one. */
    isAccount (peer)
    {
        return this.sessions ? peer.account !== null : null;
    }

    /* A peer's way into the document, gone. */
    revoke (peer)
    {
        for (const t of peer.tickets)
            this.tickets.delete(t);

        for (const d of peer.docs)
            d.terminate();
    }

    /* ---- the document socket ---- */

    /* An awareness update from `ws', as the relay will pass it on: the
       name on a cursor is the one its room socket goes by, a guest's
       marked as one, and not whatever the page put there; and no socket
       speaks for a client another peer's does. One of the same peer's
       takes the client over: that is a page's document socket
       reconnecting, the old one not yet known to be dead. */
    vouched (update, ws, owner)
    {
        const dec = decoding.createDecoder(update);
        const enc = encoding.createEncoder();
        const kept = [];
        const fresh = new Set();
        const n0 = decoding.readVarUint(dec);

        /* A page's provider sends back every state it applies, others'
           included, so an update may carry the whole room; what this
           socket may claim is counted below. */
        if (n0 > CLIENTS_PER_ROOM)
            throw new Error(`${n0} awareness states in one update`);

        for (let n = n0; n > 0; n--)
        {
            const client = decoding.readVarUint(dec);
            const clock = decoding.readVarUint(dec);
            let state = JSON.parse(decoding.readVarString(dec));
            const holder = this.clientSocket.get(client);

            if (holder !== undefined && holder !== ws &&
                this.docOwner.get(holder) !== owner)
                continue;

            /* A client nobody has, gone: it removes nothing, and the
               awareness would keep its clock for good. */
            if (holder === undefined && state === null)
                continue;

            if (holder !== undefined && holder !== ws)
            {
                this.controlled.get(holder).delete(client);
                this.controlled.get(ws).add(client);
                this.clientSocket.set(client, ws);
            }

            /* A page is one client. Every id a socket makes up is a state
               the relay keeps and hands to everyone, so a socket that
               claims more than a page could is cut, as is any past a
               room's worth. */
            if (holder === undefined && state !== null)
            {
                fresh.add(client);

                if (this.controlled.get(ws).size + fresh.size >
                        CLIENTS_PER_SOCKET ||
                    this.clientSocket.size + fresh.size > CLIENTS_PER_ROOM)
                    throw new Error('more awareness clients than a page has');
            }

            /* A state, unless it is the one saying the client is gone: and
               every one with the relay's name on it, whatever the page put
               there or left out, so that none goes nameless and unmarked. */
            if (state !== null)
            {
                if (typeof state !== 'object' || Array.isArray(state))
                    state = {};

                state.user = {
                    ...(typeof state.user === 'object' ? state.user : {}),
                    name: shownName({ name: owner.name,
                                      account: this.isAccount(owner) }),
                    account: this.isAccount(owner),
                };

                /* The colors end up in other pages' style attributes, so
                   only the shape editor.js's colourOf makes goes through. */
                for (const k of ['color', 'colorLight'])
                    if (!CURSOR_COLOR.test(String(state.user[k])))
                        delete state.user[k];
            }

            kept.push([client, clock, state]);
        }

        encoding.writeVarUint(enc, kept.length);

        for (const [client, clock, state] of kept)
        {
            encoding.writeVarUint(enc, client);
            encoding.writeVarUint(enc, clock);
            encoding.writeVarString(enc, JSON.stringify(state));
        }

        return encoding.toUint8Array(enc);
    }

    attachDoc (ws, owner)
    {
        owner.docs.add(ws);
        this.docConns.add(ws);
        this.controlled.set(ws, new Set());
        this.docOwner.set(ws, owner);
        ws.binaryType = 'arraybuffer';

        ws.on('message', (data) =>
        {
            let bytes;

            /* A socket cut for what it sent is not heard while it closes. */
            if (ws.readyState !== ws.OPEN)
                return;

            if (data instanceof ArrayBuffer)
                bytes = new Uint8Array(data);
            else if (Array.isArray(data))
                bytes = new Uint8Array(Buffer.concat(data));
            else
                bytes = new Uint8Array(data);

            this.traffic?.docIn(bytes);

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
                            this.sendDoc(ws, encoding.toUint8Array(enc));

                        break;

                    case MSG_AWARENESS:
                        awarenessProtocol.applyAwarenessUpdate(
                            this.awareness,
                            this.vouched(decoding.readVarUint8Array(dec), ws,
                                         owner),
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
            this.docOwner.delete(ws);
            this.touch();
        });

        ws.on('error', () => ws.close());

        /* Sync step 1, and whatever awareness there already is. */
        const enc = encoding.createEncoder();

        encoding.writeVarUint(enc, MSG_SYNC);
        syncProtocol.writeSyncStep1(enc, this.doc);
        this.sendDoc(ws, encoding.toUint8Array(enc));

        const states = this.awareness.getStates();

        if (states.size > 0)
        {
            const aw = encoding.createEncoder();

            encoding.writeVarUint(aw, MSG_AWARENESS);
            encoding.writeVarUint8Array(
                aw, awarenessProtocol.encodeAwarenessUpdate(
                    this.awareness, [...states.keys()]));
            this.sendDoc(ws, encoding.toUint8Array(aw));
        }
    }

    /* ---- the room socket ---- */

    attachRoom (ws)
    {
        let id = null;
        let ticketing = null;
        let chatTokens = CHAT_BURST;
        let chatAt = relayNow();

        /* The length of the line sent, in bytes; 0 if it was not. */
        const send = (m) =>
        {
            if (ws.readyState !== ws.OPEN)
                return 0;

            const line = JSON.stringify(m);

            ws.send(line);
            return this.traffic?.roomOut(m.type, line) ?? 0;
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
            let n = 0;
            const put = (p) =>
            {
                if (p !== undefined && p.ws.readyState === p.ws.OPEN)
                {
                    p.ws.send(s);
                    n++;
                }
            };

            if (to === undefined)
            {
                for (const [pid, p] of this.peers)
                    if (pid !== id)
                        put(p);
            }
            else
                for (const pid of Array.isArray(to) ? to : [to])
                    put(this.peers.get(String(pid)));

            this.traffic?.roomOut(m.type, s, n);
        };

        const others = (m) => toPeers(m);

        const seatMap = () =>
        {
            const out = {};

            for (const [seat, pid] of this.seats)
                out[seat] = pid;

            return out;
        };

        const heard = (data) =>
        {
            /* Closing is final: after a refused hello, or a session ended
               (endSessions), whatever else a client sends before the close
               completes -- up to half a minute of it -- is not heard. */
            if (ws.readyState !== ws.OPEN)
                return;

            let m;

            try
            {
                m = JSON.parse(data.toString());
            }
            catch
            {
                m = undefined;
            }

            this.traffic?.roomIn(typeof m?.type === 'string' ? m.type
                                                             : 'other',
                                 data.length);

            if (m === undefined)
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

                /* A page from before tickets would join the room and
                   wait for ever on a document socket that is never let
                   in; told now, it says so, and a reload is the fix. */
                if (m.tickets !== true)
                {
                    send({ type: 'error', why: 'old',
                           text: 'this page is older than the relay: press ' +
                                 'Update above, or close thinksynth\'s ' +
                                 'other tabs and reload' });
                    ws.close();
                    return;
                }

                /* An account plays under its handle. A session the relay
                   no longer knows is refused rather than made a guest, or
                   somebody would play the room believing they were logged
                   in. A guest goes by the name asked for, if that is not
                   an account's. */
                let account;
                let asked;

                /* A relay without accounts has no sessions to know. */
                const session = this.sessions ? m.session : undefined;

                try
                {
                    account = session === undefined ? null
                        : this.accounts.sessionAccount({ session });
                    asked = account?.handle ??
                            normalizeName(String(m.name ?? ''));

                    if (this.sessions && account === null &&
                        asked !== null && !this.accounts.nameFree(asked))
                        asked = undefined;
                }
                catch (e)
                {
                    process.stderr.write(`relay: accounts: ${e.message}\n`);
                    send({ type: 'error', why: 'accounts',
                           text: 'the relay cannot look up accounts right ' +
                                 'now; try again in a moment' });
                    ws.close();
                    return;
                }

                if (session !== undefined && account === null)
                {
                    send({ type: 'error', why: 'session',
                           text: 'your session has ended; log in again' });
                    ws.close();
                    return;
                }

                if (asked === undefined)
                {
                    send({ type: 'error', why: 'name',
                           text: `${normalizeName(String(m.name))} is an ` +
                                 'account\'s handle; log in, or pick ' +
                                 'another name' });
                    ws.close();
                    return;
                }

                /* A page joining again names the last ticket it was
                   handed, which only its own room socket ever was: the
                   peer it was, on a socket a dropped network left open
                   until the heartbeat finds it, goes now, and its seat
                   and cursor with it. */
                const was = this.tickets.get(m.was);

                if (was?.room === this && was.until > relayNow() &&
                    this.leaving.has(was.peer.ws))
                {
                    was.peer.ws.terminate();
                    this.leaving.get(was.peer.ws)();
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
                    identity: { name, account: this.isAccount(peer) },
                    ticket: this.issue(peer),
                    peers: [...this.peers].map(([pid, p]) =>
                        ({ peer: pid, name: p.name, seat: p.seat,
                           account: this.isAccount(p) })),
                    seats: seatMap(),
                    piece: this.doc.getMap('meta').get('piece') ?? null,
                    playing: this.playing,
                    /* What a page cannot assume of an older relay. */
                    features: ['switch'],
                });

                others({ type: 'joined', peer: id, name,
                         account: this.isAccount(peer) });
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

                    /* Who made it is the relay's to say: a late joiner is
                       told whose Play it catches up with, and the run's
                       commands are told apart by sender and count. */
                    m.data.from = id;

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
                        this.record(m.data, m.run, data.length);

                    others({ type: 'transport', from: id, data: m.data });
                    break;
                }

                /* A copy of a stamped command the mesh carried, kept for
                   whoever joins while this run plays. Not forwarded: the
                   others have it. */
                case 'log':
                    if (typeof m.data === 'object' && m.data !== null)
                        this.record({ ...m.data, from: id }, m.run,
                                    data.length);
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
                                   account: this.isAccount(me), text };

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
                                           account: this.isAccount(me),
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
                            const bytes = send({ type: 'catchup',
                                                 start: null });

                            this.traffic?.catchup(bytes);
                            return;
                        }

                        run.files.then((files) =>
                        {
                            if (this.run !== run)
                                answer();
                            else
                            {
                                const bytes = send({ type: 'catchup',
                                                     start: run.start, files,
                                                     log: run.log,
                                                     overflowed:
                                                         run.overflowed });

                                this.traffic?.catchup(bytes);
                            }
                        });
                    };

                    answer();
                    break;
                }
            }
        };

        /* Its fields are untrusted: one made to throw when it is read as a
           string or a number -- {"toString": 1} -- costs its own socket,
           not the whole relay. */
        ws.on('message', (data) =>
        {
            try
            {
                heard(data);
            }
            catch (err)
            {
                process.stderr.write(`relay: bad room message in room ` +
                                     `${this.name}: ${err.message}\n`);
                ws.close();
            }
        });

        /* Gone from the room: at the close, or at once when the session
           ends. */
        const leave = () =>
        {
            clearInterval(ticketing);

            const me = this.peers.get(id);

            if (id === null || me?.ws !== ws)
                return;

            this.revoke(me);

            if (me?.seat !== null && me?.seat !== undefined)
                this.seats.delete(me.seat);

            this.peers.delete(id);
            others({ type: 'left', peer: id });
            others({ type: 'seats', seats: seatMap() });
            this.touch();
        };

        ws.on('close', () =>
        {
            this.leaving.delete(ws);
            leave();
        });
        ws.on('error', () => ws.close());
        this.leaving.set(ws, leave);
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
   `trustProxy' are accountRoutes'; `passkeys' is passkeyConfig's, or null
   for none. `metricsPort' serves metrics on 127.0.0.1, or null for none;
   `server.metrics' is that server. The two times are for a harness. */
export function relay ({ port = 8787, host = '0.0.0.0',
                         tree = path.join(here, '..', '..'), db = ':memory:',
                         corsOrigin = null, trustProxy = 0, passkeys = null,
                         metricsPort = null,
                         ticketMs = TICKET_MS, heartbeatMs = HEARTBEAT_MS,
                         sessionCheckMs = SESSION_CHECK_MS } = {})
{
    const rooms = new Map();
    const tickets = new Map();          /* ticket -> { room, peer, until } */
    const traffic = metricsPort === null ? null : new Traffic();
    const started = relayNow();
    const store = new AccountStore(db, { busyMs: STORE_BUSY_MS });

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
    /* Passkeys are an account's, and there are none without accounts. */
    const keys = corsOrigin === null || passkeys === null
        ? null : new Passkeys({ accounts, ...passkeys });
    const api = accountRoutes(accounts, { corsOrigin, trustProxy,
                                          passkeys: keys });

    const room = (name, seedWith) =>
    {
        let r = rooms.get(name);

        if (r === undefined)
        {
            r = new Room(name, seedWith, tree,
                         { accounts, tickets, ticketMs, traffic,
                           sessions: corsOrigin !== null });
            rooms.set(name, r);
        }

        return r;
    };

    const server = http.createServer((req, res) =>
    {
        const url = new URL(req.url, 'http://localhost');

        /* Accounts are for the page at CORS_ORIGIN; without one there is
           no such page, and no accounts at all. */
        if (url.pathname.startsWith(`${ACCOUNT_API}/`) && corsOrigin !== null)
        {
            api(req, res);
            return;
        }

        if (url.pathname === '/')
        {
            res.writeHead(200, { 'Content-Type': 'application/json',
                                 'Access-Control-Allow-Origin': '*' });
            res.end(JSON.stringify({
                thinksynth: 'relay', protocol: PROTOCOL,

                /* Only a page at CORS_ORIGIN can use them, so without it
                   there are none to offer. */
                accounts: corsOrigin !== null,

                /* The RP ID, for the page to tell whether it is on it. */
                passkeys: keys?.rpId ?? null,
                rooms: [...rooms].map(([name, r]) =>
                    ({ name, peers: r.peers.size, piece: pieceName(r.doc),
                       playing: r.playing !== null })),
            }) + '\n');
            return;
        }

        res.writeHead(404).end();
    });

    const roomWss = new WebSocketServer({ noServer: true,
                                          maxPayload: ROOM_FRAME_MAX });
    const docWss = new WebSocketServer({ noServer: true,
                                         maxPayload: DOC_FRAME_MAX });

    /* Nothing a client sends may throw out of here: an exception in an
       upgrade listener is the whole process. */
    server.on('upgrade', (req, socket, head) =>
    {
        try
        {
            upgrade(req, socket, head);
        }
        catch (e)
        {
            process.stderr.write(`relay: upgrade failed: ${e.stack}\n`);
            socket.destroy();
        }
    });

    const upgrade = (req, socket, head) =>
    {
        const url = new URL(req.url, 'http://localhost');
        const m = /^\/(doc|room)\/([A-Za-z0-9_.-]{1,64})$/.exec(url.pathname);

        if (m === null)
        {
            socket.destroy();
            return;
        }

        /* The piece a new room is seeded with is named in the query,
           and only counts for the first socket to reach the room. An
           empty one seeds nothing: a page joining again a room the relay
           has lost brings the document itself. */
        const seedWith = url.searchParams.get('piece') ?? DEFAULT_PIECE;
        const given = url.searchParams.get('ticket');
        const ticket = m[1] === 'doc' ? tickets.get(given) : null;

        if (m[1] === 'doc' && !(ticket !== undefined &&
                                ticket.room === rooms.get(m[2]) &&
                                ticket.until > relayNow() &&
                                ticket.peer.docs.size < DOCS_PER_PEER))
        {
            socket.end('HTTP/1.1 403 Forbidden\r\n' +
                       'Connection: close\r\n\r\n');
            return;
        }

        const wss = m[1] === 'doc' ? docWss : roomWss;

        wss.handleUpgrade(req, socket, head, (ws) =>
        {
            ws.alive = true;
            ws.on('pong', () => { ws.alive = true; });

            try
            {
                /* The room socket may have gone while this one upgraded. */
                if (m[1] === 'doc' && !tickets.has(given))
                    ws.close();
                else if (m[1] === 'doc')
                    ticket.room.attachDoc(ws, ticket.peer);
                else
                    room(m[2], seedWith).attachRoom(ws);
            }
            catch (e)
            {
                process.stderr.write(`relay: upgrade failed: ${e.stack}\n`);
                ws.terminate();
            }
        });
    };

    const heartbeat = setInterval(() =>
    {
        for (const ws of [...roomWss.clients, ...docWss.clients])
        {
            if (!ws.alive)
            {
                ws.terminate();
                continue;
            }

            ws.alive = false;
            ws.ping();
        }
    }, heartbeatMs);

    heartbeat.unref();

    /* Sessions ended by the admin commands, which another process ran. */
    const recheck = setInterval(() =>
    {
        try
        {
            for (const r of rooms.values())
                r.endSessions((a) => accounts.sessionAccount(a) === null,
                              'your session has ended; log in again');
        }
        catch (e)
        {
            process.stderr.write(`relay: checking sessions: ${e.message}\n`);
        }
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

    /* On a port of its own, and on loopback whatever `host' is: behind
       nginx every request to the main port comes from loopback, so no
       check there could keep these to the people who run the host.
     *
       The event loop's delay, the timer's lag and the CPU are over a
       window, which `GET /?reset' ends after answering for it, and which
       a plain `GET /' only reads: one scraper resets, and anyone else
       looking takes nothing from its windows. `window' counts the resets,
       so a scraper can tell whether somebody else ended one.
     *
       Node's histogram is how late its timer runs, which a loop busy
       with short callbacks keeps on time: at full CPU it can read next
       to nothing, and a window it has no run in is null rather than 0.
       The CPU spent says how busy the loop is. A timer of the relay's
       own keeps its last run across scrapes, so what it is overdue by
       when an answer is made is how late that answer is. */
    let delay = null;
    let delayFrom = started;
    let lagTimer = null;
    let lagMax = 0;
    let lagAt = started;
    let cpuFrom = process.cpuUsage();
    let window = 0;

    const metrics = metricsPort === null ? null : http.createServer((req, res) =>
    {
        if (req.method !== 'GET' || (req.url !== '/' && req.url !== '/?reset'))
        {
            res.writeHead(404).end();
            return;
        }

        const now = relayNow();
        const ms = (ns) => delay.count === 0 ? null
                                             : Math.max(0, ns / 1e6 -
                                                           DELAY_RESOLUTION_MS);
        const overdue = Math.max(0, now - lagAt - DELAY_RESOLUTION_MS);
        const cpu = process.cpuUsage(cpuFrom);
        let peers = 0;
        let awarenessClients = 0;

        for (const r of rooms.values())
        {
            peers += r.peers.size;
            awarenessClients += r.clientSocket.size;
        }

        const body = {
            uptimeMs: now - started,
            window,
            eventLoopDelayMs: {
                windowMs: now - delayFrom,
                p50: ms(delay.percentile(50)), p99: ms(delay.percentile(99)),
                max: ms(delay.max), mean: ms(delay.mean),
            },
            timerLagMs: { max: Math.max(lagMax, overdue), now: overdue },
            cpuPercent: (cpu.user + cpu.system) / 10 / (now - delayFrom),
            memoryBytes: process.memoryUsage(),
            rooms: rooms.size, peers, roomSockets: roomWss.clients.size,
            docSockets: docWss.clients.size, awarenessClients,
            bufferedMaxBytes: [...roomWss.clients, ...docWss.clients]
                .reduce((max, ws) => Math.max(max, ws.bufferedAmount), 0),
            room: Object.fromEntries(traffic.room),
            doc: traffic.doc,
            catchups: traffic.catchups,
            catchupMaxBytes: traffic.catchupMaxBytes,
        };

        /* The lag up to now is this window's, and the timer's next run
           measures from here. */
        if (req.url === '/?reset')
        {
            delay.reset();
            delayFrom = now;
            lagMax = 0;
            lagAt = now;
            cpuFrom = process.cpuUsage();
            window++;
        }

        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify(body) + '\n');
    });

    if (metrics !== null)
    {
        delay = monitorEventLoopDelay({ resolution: DELAY_RESOLUTION_MS });
        delay.enable();
        lagTimer = setInterval(() =>
        {
            const t = relayNow();

            lagMax = Math.max(lagMax, t - lagAt - DELAY_RESOLUTION_MS);
            lagAt = t;
        }, DELAY_RESOLUTION_MS);
        lagTimer.unref();
    }

    /* Down, now: every socket cut, since close() alone waits for them
       and an upgraded socket is nobody's to wait for. */
    server.shutdown = () =>
    {
        clearInterval(sweep);
        clearInterval(recheck);
        clearInterval(heartbeat);

        for (const r of rooms.values())
            r.destroy();

        rooms.clear();
        store.close();
        server.closeAllConnections?.();
        server.close();
        delay?.disable();
        clearInterval(lagTimer);
        metrics?.close();
    };

    return Promise.all([
        new Promise((resolve) => server.listen(port, host, resolve)),
        metrics && new Promise((resolve) =>
            metrics.listen(metricsPort, '127.0.0.1', resolve)),
    ]).then(() =>
    {
        server.rooms = rooms;
        server.accounts = accounts;
        server.metrics = metrics;
        return server;
    });
}

if (process.argv[1] !== undefined &&
    import.meta.url === pathToFileURL(process.argv[1]).href)
{
    const args = process.argv.slice(2);

    /* DB names the accounts' file; CORS_ORIGIN the page's origin, without
       which there are no accounts; TRUST_PROXY how many proxies in front
       append to X-Forwarded-For (1 behind nginx alone); PASSKEY_RP_ID the
       site's domain, for passkeys (passkeyConfig); METRICS_PORT a port on
       127.0.0.1 to serve metrics on. */
    const opts = { db: process.env.DB || path.join(here, 'relay.db'),
                   corsOrigin: process.env.CORS_ORIGIN || null,
                   trustProxy: Number(process.env.TRUST_PROXY ?? 0),
                   metricsPort: process.env.METRICS_PORT
                       ? Number(process.env.METRICS_PORT) : null };

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

    /* An origin is what a browser sends in Origin, and nothing else: one
       with a path or a slash after it matches no request at all, and `*'
       would let any site spend its visitors' registrations here. */
    if (opts.corsOrigin !== null && !isOrigin(opts.corsOrigin))
    {
        process.stderr.write(`relay.mjs: CORS_ORIGIN is ${opts.corsOrigin}; ` +
                             'it is scheme://host[:port], nothing after\n');
        process.exit(2);
    }

    if (!(Number.isInteger(opts.trustProxy) && opts.trustProxy >= 0))
    {
        process.stderr.write('relay.mjs: TRUST_PROXY is a count of proxies\n');
        process.exit(2);
    }

    if (opts.metricsPort !== null &&
        !(Number.isInteger(opts.metricsPort) && opts.metricsPort >= 0 &&
          opts.metricsPort < 65536))
    {
        process.stderr.write('relay.mjs: METRICS_PORT is a port number\n');
        process.exit(2);
    }

    try
    {
        opts.passkeys = passkeyConfig(process.env);
    }
    catch (e)
    {
        process.stderr.write(`relay.mjs: ${e.message}\n`);
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
                         `accounts in ${opts.db}` +
                         (server.metrics === null ? '' :
                          `, metrics on http://127.0.0.1:` +
                          `${server.metrics.address().port}/`) + ')\n');
}
