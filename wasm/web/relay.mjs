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
 * A room is made when the first peer's hello is welcomed and seeded with a
 * shipped piece -- the .gen, and every .dsp it names, from the tree -- and
 * kept for a while after the last one leaves. A peer can have it seeded again
 * with another (`switch'). Rooms are not persisted; accounts are, in one
 * SQLite file (DB=..., beside the relay by default).
 */

import crypto from 'node:crypto';
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { monitorEventLoopDelay } from 'node:perf_hooks';
import v8 from 'node:v8';
import vm from 'node:vm';

import { WebSocketServer } from 'ws';
import * as Y from 'yjs';
import * as awarenessProtocol from 'y-protocols/awareness';
import * as syncProtocol from 'y-protocols/sync';
import * as decoding from 'lib0/decoding';
import * as encoding from 'lib0/encoding';

import { ACCOUNT_API, isOrigin, normalizeName, shownName } from './account.js';
import { AccountStore, Accounts, ADMIN_USAGE, Bucket as TokenBucket,
         TieredLimit, accountRoutes, clientOf, runAdmin }
    from './accounts.mjs';
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
   the revision it names before keeping what is there, and how often at
   most a room looks (snapshotAt). */
const SNAPSHOT_WAIT = 10 * 1000;
const SNAPSHOT_EVERY_MS = 100;

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
   that. A run keeps its commands as their JSON, which is what a
   catch-up sends, and is charged that: parsed, a command of empty
   objects held twenty times its JSON. A command that would take the room or the rooms past what
   they may be charged overflows the run, and so does an edit to the
   document that would: the document is the one to keep. The most is
   half what a room may be charged (ROOM_MAX_BYTES). */
const LOG_BYTES_MAX = 8 * 1024 * 1024;
const LOG_ENTRY_MAX = 512 * 1024;

/* How a catch-up ends, after the run's commands. */
const CAUGHT_UP = Buffer.from('],"overflowed":false}');
const OVERFLOWED = Buffer.from('],"overflowed":true}');

/* The most peers in a room. What the relay forwards for a room whose
   mesh has failed grows as the square of its peers: 192 players at 10
   knobs a second, all through the relay, is where one core falls behind
   (an event-loop p99 past 20 ms), and 64 is a ninth of that, which leaves
   the core to the other rooms. There are 16 seats. */
const PEERS_PER_ROOM_MAX = 64;

/* Awareness clients one document socket may speak for, and a room may
   hold: a page is one, and a reconnect briefly has the old socket's
   too. */
const CLIENTS_PER_SOCKET = 2;
const CLIENTS_PER_ROOM = 256;

/* And the longest state one may have, as JSON: a page's is a name, two
   colors and a selection, about 400 bytes. Each is kept for the room as
   those fields alone (vouched) and handed to every joiner, and is no
   charge of the room's. */
const CLIENT_STATE_MAX = 1024;

/* How long an empty room is kept, and how often that is looked at. A
   page that loses its room socket joins again by itself for about two
   minutes (jam.js, REJOIN_TRIES), and Rejoin after that brings its own
   document to a room the relay no longer has; an empty room is kept for
   five times that. */
const EMPTY_FOR_MS = 10 * 60 * 1000;
const SWEEP_EVERY_MS = 60 * 1000;

/* Rooms: at most this many, and this much heap between them by what each
   is charged -- a base, and what its document holds, measured as it is
   after every transaction (Room.watch). Past either, the rooms empty
   longest go, as many as make room for a new one or an edit, and with
   too few to let go it is refused. Measured, an empty room holds 6 KB
   of heap and is charged 8 KB, one seeded with airports.gen 18 KB and
   26 KB, and with sunrise.gen 96 KB and 165 KB. The budget is a third
   of the 384 MiB heap (docker/compose.yaml): 4096 rooms of the default
   piece, or about 800 of sunrise.gen's. One room may be charged an
   eighth of it, and an update that could take it past that is refused
   (refusal): twice the largest document frame (DOC_FRAME_MAX), twice
   over. */
const ROOMS_MAX = 4096;
const ROOMS_MAX_BYTES = 128 * 1024 * 1024;
const ROOM_MAX_BYTES = 16 * 1024 * 1024;
const ROOM_BYTES = 8 * 1024;

/* What a document holds of the heap, as it is charged: every struct
   Yjs keeps, whole or a piece split from one, and every client it keeps
   them for; every root type and every type an item holds; every value
   an item holds and every key of an object or of a map, and two bytes
   for every character of a string or a key. Measured on Node 24, a
   struct is 270 B (an item split by a deletion, or a keystroke and the
   piece of the text it split), a client 490 B and a type 300 to 600 B,
   besides the item holding it; a value in an array is 10 B (null) to
   66 B ({}), and a key in a map 190 B. Every one of these is a few
   bytes of an update at least, and none is bounded by the update's size
   alone: deleting every other character of a text splits it into one
   struct for each, at 2 B of update apiece. */
const STRUCT_BYTES = 320;
const CLIENT_BYTES = 512;
const TYPE_BYTES = 1024;
const VALUE_BYTES = 80;

/* What Yjs may hold of a room's updates whose structs build on ones its
   document does not have. Every struct a page has came through the
   relay, or is its own and came on the socket it sends on, so a page
   leaves next to nothing held, and only while a reconnect's sync catches
   up with an update sent ahead of it. Past this what is held is a
   socket's made to be held: it goes, and the socket whose frames added
   the most of it is cut, whose provider, if it is a page's, syncs
   again. Kept, it would be merged again with every update that adds to
   it, and handed to every joiner; while it is not past this it is
   charged what it could add to the document (growthOf), or twice its
   size if that is more. */
const PENDING_MAX_BYTES = 64 * 1024;

/* New rooms one client may make: a burst, then one back every
   `refillMs', per address and per IPv6 /48 (accounts.mjs, LIMITS). A
   page makes one when it joins a room nobody is in; a class behind one
   address making a room each is the burst. */
const ROOM_LIMITS = [{ burst: 20, refillMs: 3000 },
                     { burst: 60, refillMs: 1000 },
                     null];

/* Hellos one client may have welcomed, into a room old or new: a burst,
   then one back every `refillMs', per address and per IPv6 /48. A page
   joins once, and again when it loses the room, a second at the soonest
   and doubling (jam.js, REJOIN_FIRST_MS); the burst is the fullest room
   joined from one address twice over. Each join and leave is told to
   everyone in the room, and a client joining and leaving again as fast
   as it could did so 2300 times a second. */
const JOIN_LIMITS = [{ burst: 2 * PEERS_PER_ROOM_MAX, refillMs: 1000 },
                     { burst: 8 * PEERS_PER_ROOM_MAX, refillMs: 250 },
                     null];

/* The bytes of document updates the peers of one address may send
   between them, at once and then a second, and of an IPv6 /48: reading
   an update is most of what it costs, a 3.8 MB frame of 1.9 M one-byte
   structs 260 ms of the loop, and an address has as many peers as it
   has joins (JOIN_LIMITS). One peer's worth (DOC_IN_BYTES), and four
   for a /48, as for its joins: a page sends its keystrokes, and a whole
   document when it brings back a room the relay lost. Past it the
   socket is cut, as one past its own. */
const UPDATE_LIMITS = [{ burst: 16 * 1024 * 1024,
                         refillMs: 1000 / (2 * 1024 * 1024) },
                       { burst: 64 * 1024 * 1024,
                         refillMs: 1000 / (8 * 1024 * 1024) },
                       null];

/* Chat: the longest line, and how many a peer may send at once and then
   per second. Enough to talk in, and short of what a stuck key or a
   script would make of a room's screens. */
const CHAT_MAX = 500;
const CHAT_BURST = 5;
const CHAT_PER_SECOND = 5;

/* How many of each room message a socket may send at once, and then a
   second: what a page sends at most (room.js, mesh.js, jam.js), with room
   to spare. A ping goes once a second. A signal is an offer or an answer
   and a few candidates for each peer -- twenty, on a host with many
   interfaces -- and a page joining the fullest room links to 63 at once.
   A gesture is one `relayed' line for every peer the mesh cannot reach,
   and a copy for the log: a knob dragged is one per input event, 240 a
   second on a fast display, and a MIDI keyboard's notes fewer.
   `transport' is a start, a stop or an edit, or a tempo dragged. A seat,
   a switch and a catch-up (at Start, and for a Play whose document is
   late) are clicks. Anything else, a second hello among it, does nothing
   here. Chat is CHAT_BURST's.
 *
   Past one, the message is dropped, and the page told so at most once a
   second for each type; a socket that has had DROPS dropped is cut. A
   chat line the page counts, and a catch-up, are told of each time: the
   page waits to hear of every line it sent and every catch-up it asked
   for. */
const RATES = {
    ping: [10, 4],
    signal: [2048, 128],
    relayed: [1000, 500],
    log: [1000, 500],
    transport: [500, 250],
    seat: [10, 2],
    switch: [10, 2],
    catchup: [5, 1],
    chat: [CHAT_BURST, CHAT_PER_SECOND],
    other: [10, 1],
};
const DROPS = [1000, 100];

/* The bytes a socket's gestures and signals may have the relay queue for
   others -- each one's size times its recipients -- at once, and then a
   second. The most a page makes is its gestures through the relay to a
   full room, 2.3 MB a second, and a join's signals, 600 KB; the burst
   is half a socket's cap (QUEUED_MAX_BYTES). Past it a message is
   dropped as one past its rate is. */
const FANOUT_BYTES = [8 * 1024 * 1024, 4 * 1024 * 1024];

/* And for `transport', apart: a start, a stop and an edit change the
   run every peer plays, and one refused for its size would leave the
   room playing two. The longest line (ROOM_FRAME_MAX) to the fullest
   room fits at once, and one every eight seconds; a tempo dragged to
   it is 3 MB a second. The line goes to every peer as one Buffer. */
const RUN_FANOUT_BYTES = [64 * 1024 * 1024, 8 * 1024 * 1024];

/* The bytes a socket may send, at once and then a second, counted before
   a frame is read: reading it is most of what a frame costs. A room
   socket's longest line is an edit, which carries the piece and its
   files (ROOM_FRAME_MAX); a start carries a snapshot (doc.js, SEEN_MAX),
   64 KB; a join is an offer and twenty candidates for each of 63 peers,
   about 600 KB. A knob dragged is a `relayed' line naming every peer the
   mesh cannot reach and a copy for the log, 240 a second of 150 B each,
   and a tempo dragged 250 `transport' lines a second: 125 KB a second
   together. The burst is the longest line and a join, twice over; the
   rate is the longest line once a second, eight times the drags. A
   document socket's largest frames are a first sync, a resync after a
   reconnect and a whole piece pasted (DOC_FRAME_MAX), and the cursors it
   sends back are 2000 a second of 100 B (DOC_RATE): its burst is four of
   the largest, and its rate ten times the cursors. Past it a frame is
   dropped unread, as one past its rate, and counts as a drop for every
   DROP_BYTES of it: at one apiece, a flood of the longest lines, fewer a
   second than DROPS gives back, would never be cut. */
const ROOM_IN_BYTES = [4 * 1024 * 1024, 1024 * 1024];
const DOC_IN_BYTES = [16 * 1024 * 1024, 2 * 1024 * 1024];
const DROP_BYTES = 64 * 1024;

/* Document frames: a page's edits and cursor, and back again every
   awareness update it hears, which in the fullest room of 64 cursors
   moving 30 times a second is about 2000 a second. Past it a cursor is
   dropped, and a document update cuts the socket, whose provider
   reconnects and syncs again: dropped, the update would be missing until
   then. */
const DOC_RATE = [4096, 2048];

/* Sync step 1s a peer's document sockets may send between them, at once
   and then a second: each is answered with the whole document. A
   provider sends one when it connects; a few more is a page's resync or
   reconnect, and past them the socket is closed, and its provider
   connects again. A peer's and not a socket's, or a socket opened again
   would bring as many again. */
const SYNC_ASKS = [4, 0.1];

/* The rates above as relay() takes them: its `limits' over these, and
   their `room' over RATES. */
const LIMITS = { room: RATES, drops: DROPS, doc: DOC_RATE,
                 syncAsks: SYNC_ASKS, fanout: FANOUT_BYTES,
                 runFanout: RUN_FANOUT_BYTES, roomIn: ROOM_IN_BYTES,
                 docIn: DOC_IN_BYTES };

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

/* What the relay will queue for one socket, and for all of them: past
   the first the socket is cut, and past the second the one holding the
   most. A page that keeps up has next to nothing queued; 16 MiB is four
   of the largest document frames, or sixteen of the longest room lines,
   and half a minute of backlog on a 4 Mbit/s link. A catch-up or a sync
   of the whole document can be larger than that by itself (LOG_BYTES_MAX),
   so the one a socket asked for is held apart from its cap while it
   drains, and counted relay-wide once however many sockets share it. The
   total is a quarter of the 512 MiB the relay's container is given
   (docker/compose.yaml): queued frames are Buffers, outside the heap but
   inside that. A cut socket's close is waited for a few seconds, for the
   page to read why. */
const QUEUED_MAX_BYTES = 16 * 1024 * 1024;
const QUEUED_TOTAL_MAX_BYTES = 128 * 1024 * 1024;
const QUEUE_CHECK_MS = 250;
const CORK_MAX_BYTES = 64 * 1024;
const CUT_GRACE_MS = 5 * 1000;

/* Past this share of the heap V8 gives the relay, or with
   MEMORY_MAX_BYTES of the container's memory, heap and Buffers
   together, it sheds load until it is under the lower one. What the
   rooms are charged is a model of what they hold, and whatever that
   misses would otherwise take the process down. The garbage is
   collected first, so that only what is held counts. Then the empty
   rooms go, and one thing more a look (QUEUE_CHECK_MS), twice as many
   at each look it is still over, what costs nobody their session
   first: what the largest runs keep for late joiners (overflow), who
   then have the document alone. Only then a room, and everyone in
   them, told to join again shortly: the one whose charge grew the most
   since the last look, or with none grown, the one charged the most
   with its sockets' queues. A model that misses what a room holds
   charges it no more than the rest, so the count is what catches up
   with memory growing faster than one a look frees. It doubles to
   2 ** SHED_DOUBLINGS_MAX, and only at a look that sheds something
   with memory no lower than at the last. What a look frees is not seen
   at the next, which the Buffers' sweep has not reached yet: the look
   after one that shed sheds nothing. */
const SHED_HIGH = 0.75;
const SHED_LOW = 0.6;
const SHED_DOUBLINGS_MAX = 8;
const SHORT_OF_MEMORY = { why: 'memory', retryMs: 10 * 1000,
                          text: 'the relay is short of memory; joining ' +
                                'again shortly' };

/* Room sockets one address may hold open unwelcomed, and how long one
   has to say hello. Until it is welcomed a socket is in no room, which
   bounds the rest, and its pongs keep it open; a page says hello as its
   socket opens. The most is the joins an address may make at once
   (JOIN_LIMITS). */
const UNWELCOMED_MAX = 2 * PEERS_PER_ROOM_MAX;
const HELLO_WITHIN_MS = 5 * 1000;

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

/* What a socket cut for falling behind is told. */
const SLOW = 'the connection fell too far behind the room';

/* What an edit refused for its room's size is told, and one refused for
   all the rooms'. */
const TOO_BIG = { why: 'big', text: 'the room\'s document is as large as ' +
                                    'the relay keeps one' };
const RELAY_FULL = { why: 'rooms', text: 'the relay holds as much as it ' +
                                         'can; try again later' };

/* How long an account that owned a room and left has to come back for
   it: a reload, a dropped network, a laptop lid. */
const OWNER_GRACE_MS = 2 * 60 * 1000;

/* What a room may be: in the relay's list, joined by its link, or joined
   by its link with the invite in it. */
const VISIBILITIES = ['public', 'unlisted', 'private'];

/* What a spectator may not send: they play nothing, hold no seat, switch
   no piece, and have no chat to post in until the house exists. */
const MUSICIANS_ONLY = new Set(['seat', 'transport', 'log', 'relayed',
                                'switch', 'chat']);

/* The room socket's message types, each counted apart for the metrics
   port. Any other a client sends is counted as `other', so that a made-up
   type costs one counter and not one more each. */
const ROOM_TYPES = ['hello', 'welcome', 'joined', 'left', 'seat', 'seats',
                    'ping', 'pong', 'signal', 'relayed', 'transport', 'log',
                    'chat', 'switch', 'switched', 'catchup', 'ticket',
                    'room', 'set', 'role', 'remove', 'refused', 'error',
                    'other'];

const here = path.dirname(fileURLToPath(import.meta.url));

/* Room lines go as text frames, from Buffers: a string is copied into
   every socket it is written to, and a Buffer is not. */
const TEXT = { binary: false };

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

    /* `bytes' went to `n' sockets. */
    roomOut (type, bytes, n = 1)
    {
        const c = this.room.get(type) ?? this.room.get('other');

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

/* `m' as one line to every room socket in `to', counted: the line's
   length, or 0 if it went to none. */
function sendLine (queues, traffic, to, m)
{
    const line = Buffer.from(JSON.stringify(m));
    let n = 0;

    for (const ws of to)
        n += queues.send(ws, line, TEXT);

    traffic?.roomOut(m.type, line.length, n);
    return n > 0 ? line.length : 0;
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

/* A start as the relay keeps it for its run: the fields a page reads of
   one (commands.js, Maker.start), each of its type, and none of the rest
   a page may have put on it. Parsed, a megabyte of empty objects held
   twenty. */
function startOf ({ at, from, seq, origin, seed, seek, piece })
{
    const n = (x) => (typeof x === 'number' ? x : undefined);
    const s = (x) => (typeof x === 'string' ? x : undefined);

    return { type: 'transport', op: 'start', at: n(at), from, seq: n(seq),
             origin: n(origin), seed: n(seed), seek: n(seek),
             piece: { hash: s(piece?.hash), seen: s(piece?.seen) } };
}

/* A cursor as y-codemirror.next writes and reads one: an anchor and a
   head, each a Yjs relative position, or null. */
function cursorOf (c)
{
    const id = (x) => (Number.isSafeInteger(x?.client) &&
                       Number.isSafeInteger(x?.clock)
                           ? { client: x.client, clock: x.clock } : null);
    const at = (p) => ({ type: id(p.type), item: id(p.item),
                         tname: typeof p.tname === 'string' ? p.tname : null,
                         assoc: Number.isSafeInteger(p.assoc) ? p.assoc : 0 });

    return typeof c?.anchor === 'object' && c.anchor !== null &&
           typeof c.head === 'object' && c.head !== null
        ? { anchor: at(c.anchor), head: at(c.head) } : null;
}

/* Every socket's send queue, bounded (QUEUED_MAX_BYTES). A socket's
   `cut' is how it goes when it is over, and it is terminated without
   one.
 *
   A socket written to is corked until the loop has run every callback
   that was ready, so what one turn sends it goes in one write: a write a
   frame, each a syscall, was a third of the relay's time in a busy room.
   A pong waits as long, which only adds to the round trips a clock
   discards (clock.js keeps the shortest). Past CORK_MAX_BYTES it is
   written at once: the gain is in small frames, and what waits on a cork
   is not yet the kernel's, and would count against the socket's cap. */
class Queues
{
    /* `sockets' gives every socket's set. */
    constructor (sockets, { socketMax, totalMax })
    {
        this.sockets = sockets;
        this.socketMax = socketMax;
        this.totalMax = totalMax;
        this.shared = new Map();        /* held frame -> sockets holding it */
        this.sharedBytes = 0;           /* their sizes, added up */
        this.ownBytes = 0;              /* the rest, as of the last trim */
        this.corked = new Set();
        this.uncork = () =>
        {
            for (const ws of this.corked)
                ws._socket?.uncork();

            this.corked.clear();
        };
    }

    put (ws, data, opts, cb)
    {
        if (!this.corked.has(ws))
        {
            if (this.corked.size === 0)
                setImmediate(this.uncork);

            this.corked.add(ws);
            ws._socket.cork();
            ws.corkedBytes = 0;
        }

        ws.send(data, opts, cb);
        ws.corkedBytes += data.length;

        if (ws.corkedBytes >= CORK_MAX_BYTES)
        {
            this.corked.delete(ws);
            ws._socket.uncork();
        }
    }

    /* What `ws' has queued besides the frames it holds. */
    own (ws)
    {
        return ws.bufferedAmount - (ws.heldBytes ?? 0);
    }

    /* `data' onto `ws''s queue unless it has half its cap queued
       already, and no cut: for what one peer sends another, which a
       flood of it must not cost the other its socket. Whether it was
       sent. */
    offer (ws, data, opts = {})
    {
        return this.own(ws) + data.length <= this.socketMax / 2 &&
               this.send(ws, data, opts);
    }

    /* `data' onto `ws''s queue, or the socket cut if that puts it over
       its cap. Whether it was sent. */
    send (ws, data, opts = {})
    {
        if (ws.readyState !== ws.OPEN)
            return false;

        if (this.own(ws) + data.length > this.socketMax)
        {
            this.cut(ws);
            return false;
        }

        this.put(ws, data, opts);
        return true;
    }

    /* A message `ws' asked for, in `parts', which may be over the cap and
       may be shared with other sockets: held apart from the cap until it
       has drained, unless the socket holds another, which it then counts
       against. */
    sendHeld (ws, parts, opts = {})
    {
        if (ws.readyState !== ws.OPEN)
            return false;

        const n = parts.reduce((sum, p) => sum + p.length, 0);
        const holding = ws.heldBytes > 0;

        if (holding && this.own(ws) + n > this.socketMax)
        {
            this.cut(ws);
            return false;
        }

        if (holding)
        {
            parts.forEach((p, i) =>
                this.put(ws, p, { ...opts, fin: i === parts.length - 1 }));
            return true;
        }

        if (ws.held === undefined)
        {
            ws.held = new Map();
            ws.heldBytes = 0;
            ws.once('close', () =>
            {
                for (const [p, k] of ws.held)
                    for (let i = 0; i < k; i++)
                        this.release(ws, p);
            });
        }

        parts.forEach((p, i) =>
        {
            ws.held.set(p, (ws.held.get(p) ?? 0) + 1);
            ws.heldBytes += p.length;

            if (!this.shared.has(p))
                this.sharedBytes += p.length;

            this.shared.set(p, (this.shared.get(p) ?? 0) + 1);
            this.put(ws, p, { ...opts, fin: i === parts.length - 1 },
                     () => this.release(ws, p));
        });

        /* Against the total as the last trim found it, not every
           socket's again: a storm of syncs is many of these at once. */
        if (this.sharedBytes + this.ownBytes > this.totalMax)
            this.trim();

        return true;
    }

    /* One of `ws''s held frames, gone: written, or the socket closed.
       The last tells the socket's `drained', if it has one. */
    release (ws, p)
    {
        const k = ws.held.get(p);

        if (k === undefined)
            return;

        if (k === 1)
            ws.held.delete(p);
        else
            ws.held.set(p, k - 1);

        ws.heldBytes -= p.length;

        const holders = this.shared.get(p) - 1;

        if (holders === 0)
        {
            this.shared.delete(p);
            this.sharedBytes -= p.length;
        }
        else
            this.shared.set(p, holders);

        if (ws.heldBytes === 0)
            ws.drained?.();
    }

    cut (ws)
    {
        if (ws.cut === undefined)
            ws.terminate();
        else
            ws.cut();
    }

    /* Relay-wide: a held frame counted once, and each socket charged its
       share of it. Over the total, the socket holding the most goes now,
       without waiting on a close: what it holds is what the relay is
       short of. One gone already is not counted, though its queue is
       until its close comes, or it would be cut for again. What a socket
       has had queued for others is not its to free, and is bounded where
       it is sent (FANOUT_BYTES). */
    trim ()
    {
        const charged = [];
        let total = 0;

        for (const set of this.sockets())
            for (const ws of set)
            {
                if (!ws._socket || ws._socket.destroyed)
                    continue;

                let bytes = this.own(ws);

                for (const [p, k] of ws.held ?? [])
                    bytes += p.length * k / this.shared.get(p);

                total += bytes;
                charged.push([bytes, ws]);
            }

        this.ownBytes = total - this.sharedBytes;

        if (total <= this.totalMax)
            return;

        charged.sort((a, b) => b[0] - a[0]);

        for (const [bytes, ws] of charged)
        {
            if (total <= this.totalMax)
                break;

            process.stderr.write(`relay: ${total} bytes queued; cutting a ` +
                                 `socket holding ${Math.round(bytes)}\n`);
            total -= bytes;
            ws.terminate();
        }
    }
}

/* A token bucket on the relay's clock: `burst' at once, then
   `perSecond'. */
class Bucket extends TokenBucket
{
    constructor ([burst, perSecond])
    {
        super({ burst, refillMs: 1000 / perSecond }, relayNow);
    }
}

/* What a value Yjs decoded holds, by STRUCT_BYTES' measures. A key
   `__proto__' decoded sets an object's prototype, which is then one of
   its values. */
function valueBytes (value)
{
    const stack = [value];
    let bytes = 0;

    while (stack.length > 0)
    {
        const v = stack.pop();

        bytes += VALUE_BYTES;

        if (typeof v === 'string')
            bytes += 2 * v.length;
        else if (v instanceof Uint8Array)
            bytes += v.length;
        else if (Array.isArray(v))
            for (const x of v)
                stack.push(x);
        else if (typeof v === 'object' && v !== null)
        {
            const proto = Object.getPrototypeOf(v);

            if (proto !== Object.prototype && proto !== null)
                stack.push(proto);

            for (const k of Object.keys(v))
            {
                bytes += 2 * k.length;
                stack.push(v[k]);
            }
        }
    }

    return bytes;
}

/* What an item's content holds besides its struct. A deleted item's is
   a length, in the struct. */
function contentBytes (c)
{
    if (c instanceof Y.ContentString)
        return 2 * c.str.length;

    if (c instanceof Y.ContentAny || c instanceof Y.ContentJSON)
        return c.arr.reduce((sum, v) => sum + valueBytes(v), 0);

    if (c instanceof Y.ContentBinary)
        return VALUE_BYTES + c.content.length;

    if (c instanceof Y.ContentEmbed)
        return valueBytes(c.embed);

    if (c instanceof Y.ContentFormat)
        return 2 * c.key.length + valueBytes(c.value);

    /* An XML element's name, or a hook's, is its type's. */
    if (c instanceof Y.ContentType)
        return TYPE_BYTES +
               2 * (c.type.nodeName ?? c.type.hookName ?? '').length;

    /* A document of its own, which no page makes, and whose guid and
       options could be anything: no room takes one. */
    if (c instanceof Y.ContentDoc)
        return Infinity;

    return 0;
}

/* What item `s' holds of its content from offset `from' to `to', which
   a deletion of that much of it lets go of, as watch charges it: a text
   or a list is split there, and whatever else is whole or not at all. */
function partBytes (s, from, to)
{
    const { content } = s;
    const [a, b] = [Math.max(0, from), Math.min(s.length, to)];

    return content instanceof Y.ContentString ? 2 * (b - a)
        : Array.isArray(content.arr)
            ? content.arr.slice(a, b).reduce((sum, v) => sum + valueBytes(v), 0)
        : a === 0 && b === s.length ? contentBytes(content) : 0;
}

/* And with the key it is under in a map. */
function itemBytes (s)
{
    return !(s instanceof Y.Item) ? 0
        : contentBytes(s.content) +
          (s.parentSub === null ? 0 : VALUE_BYTES + 2 * s.parentSub.length);
}

/* The most `doc' can be charged more for taking `update', read from the
   two before it is applied, and 0 or less if it adds nothing: a root
   type it names that the document has not got; and every struct of it
   the document does not have, an item with what it holds and the
   struct either of its origins splits, if it falls inside one of the
   document's or the update's, or on one neither has, and the client of
   one the document has none of; and every struct a deletion of it may
   split, one at either end of its range that falls inside an item not
   yet deleted, or past what the document has; less what a deletion
   lets go of of the items it falls on that are not deleted yet. */
function growthOf (doc, update, decode = Y.decodeUpdate)
{
    const { store, share } = doc;
    const { structs, ds } = decode(update);
    const roots = new Set();
    const clients = new Set();
    const own = new Map();              /* client -> its structs here */
    let bytes = 0;

    for (const s of structs)
    {
        if (!own.has(s.id.client))
            own.set(s.id.client, []);

        own.get(s.id.client).push(s);
    }

    /* Whether an item's origin at `id' splits a struct: one at the last
       clock of a struct, or a right origin (`end' false) at the first,
       does not, and a document brought back whole has every one there.
       One on a struct neither the document nor the update has may. */
    const splits = (id, end) =>
    {
        if (id === null)
            return 0;

        const at = id.clock < Y.getState(store, id.client)
            ? store.clients.get(id.client) : own.get(id.client);
        const last = at?.at(-1);

        if (!(at?.[0].id.clock <= id.clock &&
              id.clock < last.id.clock + last.length))
            return 1;

        const s = at[Y.findIndexSS(at, id.clock)];

        return Number(s instanceof Y.Skip ||
                      id.clock !== (end ? s.id.clock + s.length - 1
                                        : s.id.clock));
    };

    for (const s of structs)
    {
        const { client, clock } = s.id;

        if (s instanceof Y.Item && typeof s.parent === 'string' &&
            !share.has(s.parent) && !roots.has(s.parent))
        {
            roots.add(s.parent);
            bytes += TYPE_BYTES + 2 * s.parent.length;
        }

        if (s instanceof Y.Skip ||
            clock + s.length <= Y.getState(store, client))
            continue;

        if (!store.clients.has(client) && !clients.has(client))
        {
            clients.add(client);
            bytes += CLIENT_BYTES;
        }

        bytes += s instanceof Y.Item
            ? STRUCT_BYTES * (1 + splits(s.origin, true) +
                              splits(s.rightOrigin, false)) + itemBytes(s)
            : STRUCT_BYTES;
    }

    for (const [client, deletes] of ds.clients)
    {
        const has = store.clients.get(client);
        const state = Y.getState(store, client);
        const cuts = (c) =>
        {
            if (c >= state)
                return c > state;

            const s = has[Y.findIndexSS(has, c)];

            return s.id.clock < c && !s.deleted;
        };

        for (const { clock, len } of deletes)
        {
            bytes += STRUCT_BYTES * (cuts(clock) + cuts(clock + len));

            for (let i = clock < state ? Y.findIndexSS(has, clock) : Infinity;
                 i < (has?.length ?? 0) && has[i].id.clock < clock + len; i++)
                if (!has[i].deleted)
                    bytes -= partBytes(has[i], clock - has[i].id.clock,
                                       clock + len - has[i].id.clock);
        }
    }

    return bytes;
}

/* Whether the sync frame `bytes', a step 2 or an update, would change
   `doc': a struct it has not got, or a deletion of an item it has not
   deleted. A page that may not edit still answers a sync with what it
   holds, which adds nothing and is let through. */
function addsTo (doc, bytes)
{
    const dec = decoding.createDecoder(bytes);

    decoding.readVarUint(dec);
    decoding.readVarUint(dec);

    const { structs, ds } = Y.decodeUpdate(decoding.readVarUint8Array(dec));
    const { store } = doc;

    if (structs.some((s) => !(s instanceof Y.Skip) &&
                            s.id.clock + s.length >
                                Y.getState(store, s.id.client)))
        return true;

    for (const [client, deletes] of ds.clients)
    {
        const has = store.clients.get(client);
        const state = Y.getState(store, client);

        for (const { clock, len } of deletes)
        {
            if (clock + len > state)
                return true;

            for (let i = Y.findIndexSS(has, clock);
                 i < has.length && has[i].id.clock < clock + len; i++)
                if (!has[i].deleted)
                    return true;
        }
    }

    return false;
}

/* The bytes of updates Yjs holds for `doc' until what they build on
   arrives. */
function pendingOf (doc)
{
    return (doc.store.pendingStructs?.update.length ?? 0) +
           (doc.store.pendingDs?.length ?? 0);
}

/* A short random id: for a peer, and for nothing else. */
/* Yjs splits a text item by slicing its string, and V8 keeps a slice of
   13 characters or more as a view of the string it was cut from: an
   item cut down to a few characters would keep the whole of what it was
   decoded from alive, which the room is not charged for (growthOf). Both
   halves are copied, so what is cut away can go. */
const splice = Y.ContentString.prototype.splice;

Y.ContentString.prototype.splice = function (offset)
{
    const right = splice.call(this, offset);

    this.str = (' ' + this.str).slice(1);
    right.str = (' ' + right.str).slice(1);
    return right;
};

function newId ()
{
    return Math.random().toString(36).slice(2, 8);
}

/* What a private room's link carries, and nobody can guess. */
function newInvite ()
{
    return crypto.randomBytes(12).toString('base64url');
}

/* Whether a hello `m' is let in, and as whom: `{ account, asked }', an
   account or the name a guest asked for, or `{ why, text }', why not.
   `accounts' is null on a relay without them, which has no sessions to
   know. */
function admit (m, accounts)
{
    if (m.protocol !== PROTOCOL)
        return { text: `protocol ${m.protocol}; this relay speaks ` +
                       `${PROTOCOL}` };

    /* A page from before tickets would join the room and wait for ever on
       a document socket that is never let in; told now, it says so, and
       a reload is the fix. */
    if (m.tickets !== true)
        return { why: 'old',
                 text: 'this page is older than the relay: press Update ' +
                       'above, or close thinksynth\'s other tabs and reload' };

    /* An account plays under its handle. A session the relay no longer
       knows is refused rather than made a guest, or somebody would play
       the room believing they were logged in. A guest goes by the name
       asked for, if that is not an account's. */
    let account;
    let asked;
    const session = accounts === null ? undefined : m.session;

    try
    {
        account = session === undefined ? null
            : accounts.sessionAccount({ session });
        asked = account?.handle ?? normalizeName(String(m.name ?? ''));

        if (accounts !== null && account === null && asked !== null &&
            !accounts.nameFree(asked))
            asked = undefined;
    }
    catch (e)
    {
        process.stderr.write(`relay: accounts: ${e.message}\n`);
        return { why: 'accounts',
                 text: 'the relay cannot look up accounts right now; try ' +
                       'again in a moment' };
    }

    if (session !== undefined && account === null)
        return { why: 'session',
                 text: 'your session has ended; log in again' };

    if (asked === undefined)
        return { why: 'name',
                 text: `${normalizeName(String(m.name))} is an account's ` +
                       'handle; log in, or pick another name' };

    return { account, asked };
}

/* One room: a document and the peers in it. `ctx' is the relay's, the
   same for every room: the `tickets' the document sockets are let in by
   and their life (`ticketMs'), the `tree' pieces are read from, whether
   a hello's session counts (`sessions'), the `traffic' counted and the
   send `queues'; the `empties', room -> true, empty longest first, the
   rooms' `costs' ({ bytes }), and `spare', whether they may be charged
   so much more for this one; the `limits' (LIMITS), the most peers and
   bytes a room may have (`peersMax', `bytesMax'), and `updatesIn',
   whether a client's address may send so many bytes of updates. */
class Room
{
    constructor (name, seedWith, ctx)
    {
        this.name = name;
        this.ctx = ctx;
        this.bytes = 0;                     /* what this one is charged  */
        this.lastBytes = 0;                 /* and was at the backstop's
                                               last look                 */
        this.grew = 0;                      /* and the change since      */
        this.gone = false;                  /* destroyed                 */
        this.structs = 0;                   /* the document's, as charged */
        this.clients = 0;                   /* and its clients           */
        this.roots = 0;                     /* and its root types        */
        this.pending = 0;                   /* what Yjs holds pending, as
                                               charged                   */
        this.held = null;                   /* and what that was         */
        this.doc = new Y.Doc();
        this.awareness = new awarenessProtocol.Awareness(this.doc);
        this.docConns = new Set();          /* document sockets          */
        this.peers = new Map();             /* id -> { ws, name, seat,
                                               account, tickets, docs,
                                               asks, docRates }          */
        this.seats = new Map();             /* seat -> peer id           */
        this.playing = null;                /* the last transport start  */

        /* Who holds the room's settings, a peer id or nobody; what the
           room is; and whether only its owner may edit the document. An
           account owner who left (`ownerAway') has OWNER_GRACE_MS to take
           it back from whoever it passed to. */
        this.owner = null;
        this.ownerAway = null;              /* { account, until }        */
        this.visibility = 'unlisted';
        this.invite = newInvite();
        this.locked = false;
        this.removed = new Set();           /* account ids               */
        this.spectating = new Set();        /* account ids made spectators */
        this.emptySince = relayNow();
        this.ctx.empties.set(this, true);

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
        this.waiter = null;                 /* the run's snapshotAt      */
        this.lookedAt = -Infinity;          /* and when it last looked   */

        /* The awareness protocol's own clients: what to forget when a
           socket closes. */
        this.controlled = new Map();        /* ws -> Set of client ids   */
        this.clientSocket = new Map();      /* client id -> its ws       */
        this.docOwner = new Map();          /* ws -> its room socket's peer */
        this.leaving = new Map();           /* room ws -> its leave()    */

        this.charge(ROOM_BYTES);
        this.watch();

        if (seedWith !== '')
            seedFiles(this.doc, seedWith, ctx.tree);

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

            /* The awareness keeps a clock for every client it has heard
               of, gone or not: a socket making up ids and saying each gone
               would grow that without bound. Dropped once the removal,
               which needs it, has gone out. */
            for (const id of removed)
                this.awareness.meta.delete(id);
        });
    }

    get empty ()
    {
        return this.peers.size === 0 && this.docConns.size === 0;
    }

    /* Every charge to the room, and so to the rooms' total, is made
       here, and none once the room is gone: what it was charged went back
       when it went, and a switch or a start of its still settling would
       otherwise give back what was given already. */
    charge (bytes)
    {
        if (this.gone)
            return;

        this.bytes += bytes;
        this.ctx.costs.bytes += bytes;
    }

    /* What the document holds, charged as it changes (STRUCT_BYTES):
       after a transaction, what every item it added holds, less the
       content of every item it deleted, which Yjs lets go of as the
       transaction ends; and once it has, every struct, client and root
       type there are, an update naming any number of the last. Yjs walks every client at every
       transaction, for its state vectors, and so does this. */
    watch ()
    {
        const { store, share } = this.doc;

        this.doc.on('afterTransaction', (tr) =>
        {
            let bytes = 0;

            for (const [client, clock] of tr.afterState)
            {
                const before = tr.beforeState.get(client) ?? 0;
                const structs = store.clients.get(client);

                for (let i = clock > before ? Y.findIndexSS(structs, before)
                                            : structs.length;
                     i < structs.length; i++)
                    bytes += itemBytes(structs[i]);
            }

            for (const [client, deletes] of tr.deleteSet.clients)
            {
                const structs = store.clients.get(client);

                for (const { clock, len } of deletes)
                    for (let i = Y.findIndexSS(structs, clock);
                         i < structs.length &&
                             structs[i].id.clock < clock + len;
                         i++)
                        bytes -= contentBytes(structs[i].content);
            }

            this.charge(bytes);
        });

        this.doc.on('afterTransactionCleanup', () =>
        {
            let structs = 0;
            let roots = 0;

            for (const s of store.clients.values())
                structs += s.length;

            for (const name of share.keys())
                if (roots++ >= this.roots)
                    this.charge(TYPE_BYTES + 2 * name.length);

            this.charge(STRUCT_BYTES * (structs - this.structs) +
                        CLIENT_BYTES * (store.clients.size - this.clients));
            this.structs = structs;
            this.clients = store.clients.size;
            this.roots = roots;
        });
    }

    /* What Yjs holds pending after a frame from `ws', charged as what it
       may add to the document when what it builds on comes, or let go of
       (PENDING_MAX_BYTES). What it grows by is the socket's that sent the
       frame, and over the most, the one that held most of it is cut. */
    settle (ws)
    {
        const { pendingStructs, pendingDs } = this.doc.store;
        const bytes = pendingOf(this.doc);
        let pending = bytes;

        if (pendingStructs?.update === this.held?.structs &&
            pendingDs === this.held?.ds)
            return;

        ws.holding = (ws.holding ?? 0) +
                     Math.max(0, bytes - (this.held?.bytes ?? 0));

        if (pending > PENDING_MAX_BYTES)
        {
            this.doc.store.pendingStructs = null;
            this.doc.store.pendingDs = null;
            pending = 0;
            [...this.docConns].reduce((most, d) =>
                (d.holding ?? 0) > most.holding ? d : most, ws).terminate();
        }
        else if (pending > 0)
            pending = Math.max(
                2 * pending,
                (pendingStructs === null ? 0
                    : growthOf(this.doc, pendingStructs.update,
                               Y.decodeUpdateV2)) +
                (pendingDs === null ? 0
                    : growthOf(this.doc, pendingDs, Y.decodeUpdateV2)));

        if (pending === 0)
            for (const d of this.docConns)
                d.holding = 0;

        this.charge(pending - this.pending);
        this.pending = pending;
        this.held = { structs: this.doc.store.pendingStructs?.update,
                      ds: this.doc.store.pendingDs,
                      bytes: pendingOf(this.doc) };
    }

    /* Why the document may not take the update in sync message `bytes',
       or null if it may: one is refused that could take the room past
       its most, or the rooms past theirs with none left that is empty to
       let go (growthOf). One that adds nothing -- a provider's step 2 in
       answer to the relay's step 1, from a page with nothing new -- or
       frees as much as it adds is taken, so a room at its most is still
       joined, read and cut down. One that
       fits only without the run's commands overflows the run
       (LOG_BYTES_MAX), and one that does not fit even then leaves them
       be. Asking whether the rooms may be charged it lets go of the empty
       rooms it needs, so it is asked once for each. */
    refusal (bytes)
    {
        const dec = decoding.createDecoder(bytes);

        decoding.readVarUint(dec);
        decoding.readVarUint(dec);

        const cost = growthOf(this.doc, decoding.readVarUint8Array(dec));
        const over = (bytes) =>
            this.bytes + bytes > this.ctx.bytesMax ? TOO_BIG
                : !this.ctx.spare(this, bytes) ? RELAY_FULL : null;

        if (cost <= 0)
            return null;

        const why = over(cost);

        if (why !== null && this.run?.bytes > 0 &&
            over(cost - this.run.bytes) === null)
        {
            this.overflow();
            return null;
        }

        return why;
    }

    broadcastDoc (bytes)
    {
        let n = 0;

        for (const ws of this.docConns)
            if (this.ctx.queues.send(ws, bytes))
                n++;

        this.ctx.traffic?.docOut(bytes, n);
    }

    /* `held' for a sync of the whole document, which can be over a
       socket's cap. */
    sendDoc (ws, bytes, held = false)
    {
        if (held ? this.ctx.queues.sendHeld(ws, [bytes])
                 : this.ctx.queues.send(ws, bytes))
            this.ctx.traffic?.docOut(bytes);
    }

    /* ---- the run ---- */

    /* A run is charged its start, its commands (record), and the
       document as its start named it, once that is known, and that as a
       catch-up's JSON once one has been asked for: a copy of every text,
       kept for as long as it plays. */
    begin (start, files = null)
    {
        this.playing = start;
        this.end();

        const run = { start,
                      files: files ?? this.snapshotAt(start.piece ?? {}),
                      head: null, chunks: [], pending: [], length: 0,
                      bytes: 0, kept: valueBytes(start), overflowed: false };

        this.run = run;
        this.charge(run.kept);
        run.files.then((snap) =>
        {
            if (this.run !== run || snap === null)
                return;

            const bytes = 2 * Object.values(snap.files)
                .reduce((n, text) => n + text.length, 0);

            run.kept += bytes;
            this.charge(bytes);
        });
    }

    /* The run's commands, its document, what they were charged and the
       wait for it, gone. */
    end ()
    {
        this.waiter?.(null);
        this.charge(-((this.run?.bytes ?? 0) + (this.run?.kept ?? 0)));
        this.run = null;
    }

    /* The run made one that cannot be caught up with: what it kept is of
       no use to anyone and goes, and the head of its catch-up with it,
       made again when one is asked for (catchupOf). */
    overflow ()
    {
        const { run } = this;
        const head = run.head?.length ?? 0;

        this.charge(-(run.bytes + head));
        run.overflowed = true;
        run.bytes = 0;
        run.kept -= head;
        run.head = null;
        run.chunks = [];
        run.pending = [];
    }

    /* What the run keeps for late joiners that it can do without, which
       overflow lets go of. */
    get spareBytes ()
    {
        return (this.run?.bytes ?? 0) + (this.run?.head?.length ?? 0);
    }

    /* The catch-up for `run' as it stands, in parts. Commands no
       catch-up has had yet are joined into one now, once, and every part
       is shared by every catch-up that has it: twenty joiners of a long
       run otherwise each cost a copy of all of it. An overflowed run
       sends no commands, which the page does not use (jam.js, joinRun),
       and the document, which it does (loadPassed). */
    catchupOf (run, files)
    {
        if (run.head === null)
        {
            run.head = Buffer.from(
                JSON.stringify({ type: 'catchup', start: run.start, files })
                    .slice(0, -1) + ',"log":[');
            run.kept += run.head.length;
            this.charge(run.head.length);
        }

        if (run.overflowed)
            return [run.head, OVERFLOWED];

        if (run.pending.length > 0)
        {
            const s = run.pending.join(',');

            run.chunks.push(Buffer.from(run.chunks.length === 0 ? s
                                                                : `,${s}`));
            run.pending = [];
        }

        return [run.head, ...run.chunks, CAUGHT_UP];
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
        sendLine(this.ctx.queues, this.ctx.traffic,
                 [...this.peers.values()].map((p) => p.ws),
                 { type: 'transport', from: RELAY, data: start });
    }

    /* A stamped command, into the run it was made in. `runKey' is that
       run's start, as the sender knew it (runKeyOf); a copy that arrives
       after another Play has begun is the old run's straggler, stamped for
       a time in a piece that is no longer playing, and is not kept. Past
       the run's caps, or past what its room or the rooms may be charged,
       it overflows. */
    record (cmd, runKey)
    {
        const { run } = this;

        if (run === null || runKey !== runKeyOf(run.start) ||
            run.overflowed)
            return;

        const json = JSON.stringify(cmd);
        const bytes = Buffer.byteLength(json);

        if (run.length >= LOG_MAX || bytes > LOG_ENTRY_MAX ||
            run.bytes + bytes > LOG_BYTES_MAX ||
            this.bytes + bytes > this.ctx.bytesMax ||
            !this.ctx.spare(this, bytes))
            this.overflow();
        else
        {
            run.pending.push(json);
            run.length++;
            run.bytes += bytes;
            this.charge(bytes);
        }
    }

    /* The document at the revision `hash' names: now, if the relay's copy
       is there, or once an update brings it there. The starter sent its
       edits on the document socket before its Play on this one, but the
       two are separate sockets and nothing orders them. Past the wait,
       what is here, marked as not what was asked for. One start's is
       waited for at a time, its run's: the next start, a stop or the
       room going resolves it with null, which nobody reads, its run
       being gone. A look is a snapshot of the whole document and its
       hash, and is made no more often than SNAPSHOT_EVERY_MS in a room,
       however many updates and starts come. */
    snapshotAt ({ hash, seen } = {})
    {
        const passed = seen === undefined ? null : readSeen(seen);

        return new Promise((resolve) =>
        {
            let timer = null;
            let looking = false;
            let again = false;

            const finish = (snap, matched) =>
            {
                if (this.waiter !== finish)
                    return;

                this.waiter = null;
                clearTimeout(timer);
                clearTimeout(lapse);
                this.doc.off('update', soon);
                resolve(snap === null ? null : { ...snap, matched });
            };

            const lapse = setTimeout(() => finish(snapshot(this.doc), false),
                                     SNAPSHOT_WAIT);

            const look = async () =>
            {
                timer = null;
                looking = true;
                this.lookedAt = relayNow();

                /* The snapshot first and its hash after, so what is
                   handed over is what was hashed: an update landing
                   during the digest would otherwise be in the one and
                   not the other. */
                const snap = snapshot(this.doc);
                const same = await hashOfFiles(snap.files) === hash;

                looking = false;

                if (this.waiter !== finish)
                    return;

                if (same)
                    finish(snap, true);

                /* Gone past it: no update brings it back. */
                else if (passed !== null && hasSeen(this.doc, passed))
                    finish(snap, false);
                else if (again)
                {
                    again = false;
                    soon();
                }
            };

            const soon = () =>
            {
                if (looking)
                    again = true;
                else if (timer === null)
                    timer = setTimeout(look, Math.max(
                        0, this.lookedAt + SNAPSHOT_EVERY_MS - relayNow()));
            };

            this.waiter?.(null);
            this.waiter = finish;
            this.doc.on('update', soon);
            soon();
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
            if (!(this.ctx.tickets.get(t)?.until > now))
            {
                this.ctx.tickets.delete(t);
                peer.tickets.delete(t);
            }

        this.ctx.tickets.set(ticket, { room: this, peer,
                                   until: now + this.ctx.ticketMs });
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
                this.drop(p.ws, 'session', why);
    }

    /* A room socket told why and closed, and out of the room now. Its
       last word goes past its cap, and a client that does not read it
       is cut in a few seconds. */
    drop (ws, why, text)
    {
        this.tell(ws, { why, text });
        this.leaving.get(ws)?.();
    }

    tell (ws, error)
    {
        const line = Buffer.from(JSON.stringify({ type: 'error', ...error }));

        ws.send(line, TEXT);
        this.ctx.traffic?.roomOut('error', line.length);
        ws.close();
        setTimeout(() => ws.terminate(), CUT_GRACE_MS).unref();
    }

    /* Whether a peer is an account, as the room is told: null on a relay
       without accounts, where nobody is a guest for not being one. */
    isAccount (peer)
    {
        return this.ctx.sessions ? peer.account !== null : null;
    }

    /* A peer's way into the document, gone. */
    revoke (peer)
    {
        for (const t of peer.tickets)
            this.ctx.tickets.delete(t);

        for (const d of peer.docs)
            d.terminate();
    }

    /* Whether `peer' may change the document. */
    mayEdit (peer)
    {
        return peer.role === 'musician' &&
               (!this.locked || peer.id === this.owner);
    }

    /* What the room is, as `p' is told it: the invite only to a musician,
       who may bring somebody in. */
    roomLine (p)
    {
        return { type: 'room', owner: this.owner,
                 visibility: this.visibility, locked: this.locked,
                 roles: Object.fromEntries(
                     [...this.peers].map(([id, q]) => [id, q.role])),
                 invite: p.role === 'musician' ? this.invite : null };
    }

    tellRoom ()
    {
        for (const p of this.peers.values())
            sendLine(this.ctx.queues, this.ctx.traffic, [p.ws],
                     this.roomLine(p));
    }

    /* Whether `account' is the owner's, here or within its grace: let
       into its private room without the invite, which its page may not
       have. */
    ownedBy (account)
    {
        return account !== null &&
               (this.peers.get(this.owner)?.account?.id === account.id ||
                (this.ownerAway?.account === account.id &&
                 this.ownerAway.until > relayNow()));
    }

    /* `p', and every other tab of its account. */
    alike (p)
    {
        return p.account === null ? [p]
            : [...this.peers.values()].filter(
                (q) => q.account?.id === p.account.id);
    }

    /* The owner gone: the room passes to whoever has been in it longest,
       a musician first, made one if they are not. An account owner may
       come back for it. */
    passOwner (gone)
    {
        /* An earlier owner's claim, still live, outlasts one who only
           held the room meanwhile. */
        if (gone.account !== null && !(this.ownerAway?.until > relayNow()))
            this.ownerAway = { account: gone.account.id,
                               until: relayNow() + OWNER_GRACE_MS };

        const peers = [...this.peers.values()];
        const next = peers.find((p) => p.role === 'musician') ?? peers[0];

        this.owner = next?.id ?? null;

        if (next !== undefined)
            next.role = 'musician';
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
            const json = decoding.readVarString(dec);

            if (json.length > CLIENT_STATE_MAX)
                throw new Error(`an awareness state of ${json.length} ` +
                                'characters');

            let state = JSON.parse(json);
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

            /* A state, unless it is the one saying the client is gone, as
               the fields a page's has and no others: parsed, 4 KiB of
               empty objects held 85 KiB. Every one has the relay's name
               on it, whatever the page put there or left out, so that
               none goes nameless and unmarked. */
            if (state !== null)
            {
                const user = state.user ?? {};

                state = {
                    user: { name: shownName({ name: owner.name,
                                              account: this.isAccount(owner) }),
                            account: this.isAccount(owner) },
                    cursor: cursorOf(state.cursor),
                };

                /* The colors end up in other pages' style attributes, so
                   only the shape editor.js's colourOf makes goes through. */
                for (const k of ['color', 'colorLight'])
                    if (CURSOR_COLOR.test(String(user[k])))
                        state.user[k] = user[k];
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

        /* The peer's, across its document sockets, as its sync asks
           are: a socket opened again would bring them all back. */
        const { frames, inBytes, drops } = owner.docRates;

        /* Said on the room socket, which a page shows: a document socket
           has nothing to say it in. */
        ws.cut = () =>
        {
            if (owner.ws.readyState === owner.ws.OPEN)
                this.drop(owner.ws, 'slow', SLOW);

            ws.terminate();
        };

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

            this.ctx.traffic?.docIn(bytes);

            if (!inBytes.take(bytes.length) || !frames.take())
            {
                if (bytes[0] !== MSG_AWARENESS ||
                    !drops.take(Math.ceil(bytes.length / DROP_BYTES)))
                    ws.terminate();

                return;
            }

            /* A frame is told apart here by its first bytes, its type
               and a sync message's step, which y-websocket writes as one
               byte each. Written in more, a step 1 here would be an
               update to the decoder, taken without being read for what
               it adds. */
            if (bytes[0] > 0x7f || (bytes[0] === MSG_SYNC && bytes[1] > 0x7f))
            {
                ws.close();
                return;
            }

            if (bytes[0] === MSG_SYNC &&
                bytes[1] === syncProtocol.messageYjsSyncStep1 &&
                !owner.asks.take())
            {
                ws.close();
                return;
            }

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
                    {
                        if (bytes[1] !== syncProtocol.messageYjsSyncStep1 &&
                            !this.ctx.updatesIn(owner.client, bytes.length))
                        {
                            ws.terminate();
                            break;
                        }

                        const no =
                            bytes[1] === syncProtocol.messageYjsSyncStep1
                                ? null
                            : !this.mayEdit(owner) && addsTo(this.doc, bytes)
                                ? { why: owner.role === 'spectator'
                                             ? 'spectator' : 'locked',
                                    text: owner.role === 'spectator'
                                        ? 'a spectator cannot edit the piece'
                                        : 'the room\'s owner has locked ' +
                                          'the piece' }
                            : this.refusal(bytes);

                        /* Said on the room socket, and the document socket
                           closed, whose provider would only send it again;
                           the room socket stays, and its page with it. */
                        if (no !== null)
                        {
                            sendLine(this.ctx.queues, this.ctx.traffic,
                                     [owner.ws],
                                     { type: 'refused', of: 'edit', ...no });
                            ws.terminate();
                            break;
                        }

                        encoding.writeVarUint(enc, MSG_SYNC);
                        syncProtocol.readSyncMessage(dec, enc, this.doc, ws);
                        this.settle(ws);

                        /* A reply only when there is one: step 2 in answer
                           to step 1, or nothing in answer to an update. */
                        if (encoding.length(enc) > 1)
                            this.sendDoc(ws, encoding.toUint8Array(enc),
                                         true);

                        break;
                    }

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

    /* A room socket whose hello `m' the relay has let in (admit), as
       `account', or a guest by the name `asked', from `client'. */
    attachRoom (ws, m, { account, asked }, client)
    {
        let id = null;
        let ticketing = null;
        const buckets = new Map();          /* RATES' type -> its Bucket  */
        const told = new Map();             /* type -> when it was told  */
        const drops = new Bucket(this.ctx.limits.drops);
        const inBytes = new Bucket(this.ctx.limits.roomIn);
        let answering = false;              /* a catch-up awaits its run */
        let again = false;                  /* and one waits for another
                                               to drain                  */

        ws.cut = () => this.drop(ws, 'slow', SLOW);
        const fanout = new Bucket(this.ctx.limits.fanout);
        const runFanout = new Bucket(this.ctx.limits.runFanout);

        const send = (m) =>
            sendLine(this.ctx.queues, this.ctx.traffic, [ws], m);

        /* To the peers `to' names -- one id, or a list of them, each
           once and no more than a room holds -- or, with no `to' at all,
           to everyone in the room but this one. Whether it went: what
           this socket says for others to hear comes out of the bucket
           `said' (FANOUT_BYTES, RUN_FANOUT_BYTES), and past it does not;
           what the relay says of it -- a join, a seat, a leave -- goes
           regardless.
         *
           Serialised once, however many sockets it goes to: a command to
           a room of eight otherwise costs eight identical stringify calls
           on the one path that is meant to be cheap, because nothing here
           reads what it forwards. An id nobody is on is not an error: a
           peer that left is a peer that left. A gesture or a signal is
           not queued for a peer already half way to its cap (offer). */
        const toPeers = (m, to, said = null) =>
        {
            const s = Buffer.from(JSON.stringify(m));
            const ids = to === undefined ? null
                : new Set((Array.isArray(to) ? to.slice(0, this.ctx.peersMax)
                                             : [to]).map(String));
            const lossy = m.type === 'relayed' || m.type === 'signal';
            let n = 0;
            const put = (p) =>
            {
                if (p !== undefined &&
                    (lossy ? this.ctx.queues.offer(p.ws, s, TEXT)
                           : this.ctx.queues.send(p.ws, s, TEXT)))
                    n++;
            };

            if (said !== null &&
                !said.take(s.length * (ids?.size ?? this.peers.size - 1)))
                return false;

            if (ids === null)
            {
                for (const [pid, p] of this.peers)
                    if (pid !== id)
                        put(p);
            }
            else
                for (const pid of ids)
                    put(this.peers.get(pid));

            this.ctx.traffic?.roomOut(m.type, s.length, n);
            return true;
        };

        /* `m', of RATES' `type', dropped, as `n' drops. */
        const refuse = (type, m, n = 1) =>
        {
            const now = relayNow();

            if (!drops.take(n))
                this.drop(ws, 'flood', 'this page sent more than the ' +
                                       'relay takes; rejoin');
            else if ((type === 'chat' && Number.isSafeInteger(m.n)) ||
                     type === 'catchup' ||
                     !(now - told.get(type) < 1000))
            {
                /* With what the page matches it to: the piece it asked
                   for, its chat line, or what its transport command was. */
                told.set(type, now);
                send({ type: 'refused', of: type, why: 'too fast; slow down',
                       ...(type === 'switch' &&
                           { piece: typeof m.piece === 'string'
                                 ? m.piece : '' }),
                       ...(type === 'chat' && Number.isSafeInteger(m.n) &&
                           { n: m.n }),
                       ...(type === 'transport' &&
                           typeof (m.data?.op ?? m.data?.type) === 'string' &&
                           { op: m.data.op ?? m.data.type }) });
            }
        };

        const seatMap = () =>
        {
            const out = {};

            for (const [seat, pid] of this.seats)
                out[seat] = pid;

            return out;
        };

        /* A late joiner, ready to play: the run as the relay has it, or
           `start: null' when nothing is playing. A Play while the snapshot
           is awaited makes the answer that run, which is what the joiner
           should hear, and an answer still being made answers this ask as
           well: a page takes one answer for every catch-up it is waiting
           on (room.js). One made already, and still draining, may be for
           a run since replaced: this ask is answered again once it has
           drained, and asks meanwhile with it. */
        const answer = () =>
        {
            const run = this.run;

            if (run === null)
            {
                answering = false;
                const bytes = send({ type: 'catchup', start: null });

                this.ctx.traffic?.catchup(bytes);
                ws.drained();
                return;
            }

            run.files.then((files) =>
            {
                if (this.run !== run)
                    answer();
                else
                {
                    const parts = this.catchupOf(run, files);
                    const bytes = parts.reduce((sum, p) => sum + p.length, 0);

                    answering = false;

                    if (!this.ctx.queues.sendHeld(ws, parts, TEXT))
                        return;

                    this.ctx.traffic?.roomOut('catchup', bytes);
                    this.ctx.traffic?.catchup(bytes);
                }
            });
        };
        const catchup = () =>
        {
            if (answering)
                return;

            again = ws.heldBytes > 0;

            if (!again)
            {
                answering = true;
                answer();
            }
        };

        ws.drained = () =>
        {
            if (again && ws.readyState === ws.OPEN)
                catchup();
        };

        const heard = (data) =>
        {
            /* Closing is final: after a refused hello, or a session ended
               (endSessions), whatever else a client sends before the close
               completes -- up to half a minute of it -- is not heard. */
            if (ws.readyState !== ws.OPEN)
                return;

            if (!inBytes.take(data.length))
            {
                this.ctx.traffic?.roomIn('other', data.length);
                refuse('bytes', undefined, Math.ceil(data.length / DROP_BYTES));
                return;
            }

            let m;

            try
            {
                m = JSON.parse(data.toString());
            }
            catch
            {
                m = undefined;
            }

            this.ctx.traffic?.roomIn(typeof m?.type === 'string' ? m.type
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

            const type = Object.hasOwn(this.ctx.limits.room, m.type) ? m.type
                                                                : 'other';

            if (!buckets.has(type))
                buckets.set(type, new Bucket(this.ctx.limits.room[type]));

            let forward = true;

            if (!buckets.get(type).take())
            {
                refuse(type, m);

                /* Played all the same if the mesh carried it (transport),
                   unless the refusal cut the socket. */
                if (type !== 'transport' || m.data?.type !== 'transport' ||
                    ws.readyState !== ws.OPEN)
                    return;

                forward = false;
            }

            const me = this.peers.get(id);

            /* Refused here, whatever a page shows. A gesture or a log copy
               is dropped unsaid: there would be one a keystroke. */
            if (me.role === 'spectator' && MUSICIANS_ONLY.has(m.type))
            {
                if (m.type !== 'relayed' && m.type !== 'log')
                    send({ type: 'refused', of: m.type,
                           why: 'a spectator cannot',
                           ...(m.type === 'chat' &&
                               Number.isSafeInteger(m.n) && { n: m.n }),
                           ...(m.type === 'switch' &&
                               { piece: String(m.piece ?? '') }) });
                return;
            }

            if (['set', 'role', 'remove'].includes(m.type) &&
                id !== this.owner)
            {
                send({ type: 'refused', of: m.type,
                       why: 'only the room\'s owner can' });
                return;
            }

            switch (m.type)
            {
                /* The room's settings, either or both. Made private, it
                   has a new invite: whoever had the old one, a removed
                   peer among them, is not in by it. */
                case 'set':
                    if (m.visibility === 'private' &&
                        this.visibility !== 'private')
                        this.invite = newInvite();

                    if (VISIBILITIES.includes(m.visibility))
                        this.visibility = m.visibility;

                    if (typeof m.locked === 'boolean')
                        this.locked = m.locked;

                    this.tellRoom();
                    break;

                /* A peer made a spectator, or a musician again. A
                   spectator's seat goes; an account stays what it was
                   made if it joins again. The owner is a musician. */
                case 'role':
                {
                    const p = this.peers.get(String(m.peer));

                    if (p === undefined || p.id === this.owner ||
                        !['musician', 'spectator'].includes(m.role))
                        break;

                    if (p.account !== null)
                    {
                        if (m.role === 'spectator')
                            this.spectating.add(p.account.id);
                        else
                            this.spectating.delete(p.account.id);
                    }

                    let unseated = false;

                    for (const q of this.alike(p))
                    {
                        if (q.id === this.owner)
                            continue;

                        q.role = m.role;

                        if (m.role === 'spectator' && q.seat !== null)
                        {
                            this.seats.delete(q.seat);
                            q.seat = null;
                            unseated = true;
                        }
                    }

                    if (unseated)
                    {
                        const seats = { type: 'seats', seats: seatMap() };

                        send(seats);
                        toPeers(seats);
                    }

                    this.tellRoom();
                    break;
                }

                /* A peer out of the room. An account stays out; a private
                   room's link changes, so the one they had stops
                   working, and the musicians are told the new one. */
                case 'remove':
                {
                    const p = this.peers.get(String(m.peer));

                    /* Not the owner, nor another tab of theirs. */
                    if (p === undefined || p.id === id ||
                        (p.account !== null && p.account.id === me.account?.id))
                        break;

                    if (p.account !== null)
                        this.removed.add(p.account.id);

                    this.invite = newInvite();

                    for (const q of this.alike(p))
                        this.drop(q.ws, 'removed',
                                  'the room\'s owner removed you from it');
                    break;
                }

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
                    toPeers(reply);
                    break;
                }

                case 'ping':
                    send({ type: 'pong', t0: m.t0, t1: relayNow() });
                    break;

                /* Signalling: opaque, and to one peer always -- hence
                   the String, which turns a missing `to' into an id
                   nobody has rather than into the whole room. */
                case 'signal':
                    if (!toPeers({ type: 'signal', from: id, data: m.data },
                                 String(m.to), fanout))
                        refuse(type, m);
                    break;

                /* A gesture the mesh could not carry: forwarded unread
                   (section 5.5). `to' is one peer, or the list of peers
                   whose channel is not up -- a page falls back one peer
                   at a time and sends one message for all of them. With
                   no `to' at all it goes to the room. */
                case 'relayed':
                    if (!toPeers({ type: 'relayed', from: id, data: m.data },
                                 m.to, fanout))
                        refuse(type, m);
                    break;

                /* A transport start goes here as well as over the mesh:
                   it is the one command a peer must not miss, and the
                   last one is what a joining peer is told (section 5.3).
                   A stop clears it. */
                case 'transport':
                {
                    if (typeof m.data !== 'object' || m.data === null)
                        break;

                    /* An edit or a pick changes the piece as much as a
                       keystroke does, and a lock holds them too. */
                    if ((m.data.type === 'edit' || m.data.type === 'pick') &&
                        !this.mayEdit(me))
                    {
                        send({ type: 'refused', of: 'transport',
                               why: 'the room\'s owner has locked the piece',
                               op: m.data.type });
                        break;
                    }

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

                    if (m.data.op === 'start')
                        m.data = startOf(m.data);

                    /* A start, a stop or a tempo goes over the mesh as
                       well (jam.js, send), and changes the run here
                       whether or not it goes on from here: the run is the
                       one the peers play. An edit goes from here alone,
                       and changes it only if it goes. */
                    if (forward && !toPeers({ type: 'transport', from: id,
                                              data: m.data }, undefined,
                                            runFanout))
                    {
                        refuse(type, m);
                        forward = false;
                    }

                    if ((!forward && m.data.type !== 'transport') ||
                        ws.readyState !== ws.OPEN)
                        break;

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
                            this.end();
                        }
                    }
                    else
                        this.record(m.data, m.run);

                    break;
                }

                /* A copy of a stamped command the mesh carried, kept for
                   whoever joins while this run plays. Not forwarded: the
                   others have it. */
                case 'log':
                    if (typeof m.data === 'object' && m.data !== null)
                        this.record({ ...m.data, from: id }, m.run);
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
                    const n = Number.isSafeInteger(m.n) ? m.n : undefined;

                    /* Format characters alone are a line that shows as
                       nothing, or reorders the lines around it. */
                    const why = m.channel !== 'stage' ? 'no such channel'
                              : text.length > CHAT_MAX
                                  ? `longer than ${CHAT_MAX} characters`
                              : text.replace(/\p{Cf}/gu, '').trim() === ''
                                  ? 'nothing to send'
                              : null;

                    if (why !== null)
                    {
                        send({ type: 'refused', of: 'chat', why, n });
                        break;
                    }

                    const line = { type: 'chat', channel: m.channel, from: id,
                                   name: me.name,
                                   account: this.isAccount(me), text };

                    if (typeof m.bar === 'string' &&
                        /^\d{1,6}\.\d{1,2}$/.test(m.bar))
                        line.bar = m.bar;

                    send({ ...line, n });
                    toPeers(line);
                    break;
                }

                /* Another shipped piece for the room, at one peer's word.
                   Here and not on the pages, so that two switches at once
                   are one after the other rather than two peers' writes
                   merged into a document holding both pieces. Everyone is
                   told who switched, and the revision the document is at
                   after it. While the room plays, the relay plays the last
                   of the switches made together (playSwitch). A switch
                   leaves what it replaced in the document as deletions,
                   and is not an update read before it is taken: a room
                   past its most, or a relay past its budget, makes none,
                   and one goes past them by a piece at most. */
                case 'switch':
                {
                    const piece = typeof m.piece === 'string' ? m.piece : '';
                    const full = !this.mayEdit(me)
                        ? { text: 'the room\'s owner has locked the piece' }
                        : this.bytes > this.ctx.bytesMax ? TOO_BIG
                        : !this.ctx.spare(this, 0) ? RELAY_FULL : null;

                    if (full !== null)
                    {
                        send({ type: 'refused', of: 'switch',
                               why: full.text, piece });
                        break;
                    }

                    if (piece !== path.basename(piece) ||
                        !piece.endsWith('.gen') ||
                        !seedFiles(this.doc, piece, this.ctx.tree))
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
                            toPeers(line);

                            if (playingThen === null)
                                return;

                            await new Promise((r) =>
                                setTimeout(r, SWITCH_GATHER_MS));

                            if (!this.gone && this.playing === playingThen &&
                                made === this.switches)
                                this.playSwitch(hash, seen, texts);
                        });
                    break;
                }

                case 'catchup':
                    catchup();
                    break;
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
        /* `replaced' by the same page joining again, which keeps what it
           held: the room is not passed on, and the room is told once it
           is back. */
        const leave = (replaced = false) =>
        {
            clearInterval(ticketing);

            const me = this.peers.get(id);

            if (id === null || me?.ws !== ws)
                return;

            this.revoke(me);

            if (me?.seat !== null && me?.seat !== undefined)
                this.seats.delete(me.seat);

            this.peers.delete(id);

            if (this.owner === id && !replaced)
                this.passOwner(me);

            toPeers({ type: 'left', peer: id });
            toPeers({ type: 'seats', seats: seatMap() });

            if (!replaced)
                this.tellRoom();

            this.touch();
        };

        ws.on('close', () =>
        {
            this.leaving.delete(ws);
            leave(false);
        });
        ws.on('error', () => ws.close());
        this.leaving.set(ws, leave);

        /* The peer it was, on a socket a dropped network left open until
           the heartbeat finds it, goes now, and its seat and cursor with
           it. */
        const was = this.rejoining(m.was, account);
        const wasPeer = was === null ? null : this.ctx.tickets.get(m.was).peer;
        const wasOwner = wasPeer !== null && wasPeer.id === this.owner;

        if (was !== null)
        {
            was.terminate();
            this.leaving.get(was)(true);
        }

        id = newId();

        while (this.peers.has(id) || id === RELAY)
            id = newId();

        const name = asked ?? id;

        /* The room's first, or first since it emptied, owns it; the owner
           joining again, or an account owner back within its grace, takes
           it back. Somebody made a spectator stays one. */
        const back = account !== null && this.ownerAway?.account === account.id &&
                     this.ownerAway.until > relayNow();
        const owns = wasOwner || back || this.owner === null;
        const role = owns ? 'musician'
            : wasPeer?.role ?? (account !== null &&
                                this.spectating.has(account.id)
                                    ? 'spectator' : 'musician');

        if (back)
            this.ownerAway = null;

        const peer = { id, ws, name, seat: null, account, client, role,
                       tickets: new Set(), docs: new Set(),
                       asks: new Bucket(this.ctx.limits.syncAsks),
                       docRates: { frames: new Bucket(this.ctx.limits.doc),
                                   inBytes: new Bucket(this.ctx.limits.docIn),
                                   drops: new Bucket(
                                       this.ctx.limits.drops) } };

        this.peers.set(id, peer);
        this.ctx.empties.delete(this);

        if (owns)
            this.owner = id;

        ticketing = setInterval(
            () => send({ type: 'ticket', ticket: this.issue(peer) }),
            this.ctx.ticketMs * 2 / 5);

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
            features: ['switch', 'roles'],
            room: this.roomLine(peer),
        });

        toPeers({ type: 'joined', peer: id, name,
                 account: this.isAccount(peer) });
        this.tellRoom();
    }

    /* The room socket a hello joining again replaces, or null. A page
       joining again names the last ticket it was handed, which only its
       own room socket ever was. */
    rejoining (ticket, account)
    {
        const was = this.ctx.tickets.get(ticket);

        /* Only as whoever it was: a ticket seen in a log is not a way to
           take over somebody's peer, and with it their room. */
        return was?.room === this && was.until > relayNow() &&
               (was.peer.account?.id ?? null) === (account?.id ?? null) &&
               this.leaving.has(was.peer.ws) ? was.peer.ws : null;
    }

    touch ()
    {
        if (this.empty && !this.gone)
        {
            this.emptySince = relayNow();
            this.ctx.empties.delete(this);
            this.ctx.empties.set(this, true);
        }
    }

    /* Every socket cut and every timer stopped: the awareness keeps one
       of its own, to age out states, and a room that only dropped its
       document would keep the process alive by it. Its people are told
       `error' (tell) if there is one. */
    destroy (error = null)
    {
        this.charge(-this.bytes);
        this.gone = true;
        this.end();

        for (const ws of this.docConns)
            ws.terminate();

        for (const p of this.peers.values())
            if (error !== null && p.ws.readyState === p.ws.OPEN)
                this.tell(p.ws, error);
            else
                p.ws.terminate();

        this.awareness.destroy();
        this.doc.destroy();
        this.ctx.empties.delete(this);

        /* A socket told why holds the room until its close completes,
           a few seconds (tell): what the room holds goes now. */
        this.playing = this.held = null;
        this.doc = new Y.Doc();
        this.awareness = new awarenessProtocol.Awareness(this.doc);
        this.awareness.destroy();
    }
}

/* The server. Resolves with it listening; `address().port' says where.
   `db' is the accounts' file, or ':memory:'; `corsOrigin' and
   `trustProxy' are accountRoutes'; `passkeys' is passkeyConfig's, or null
   for none. `metricsPort' serves metrics on 127.0.0.1, or null for none;
   `server.metrics' is that server, and one that cannot listen rejects.
   The two times, the caps and `limits', over LIMITS, are for a harness. */
export function relay ({ port = 8787, host = '0.0.0.0',
                         tree = path.join(here, '..', '..'), db = ':memory:',
                         corsOrigin = null, trustProxy = 0, passkeys = null,
                         metricsPort = null,
                         ticketMs = TICKET_MS, heartbeatMs = HEARTBEAT_MS,
                         sessionCheckMs = SESSION_CHECK_MS,
                         queuedMaxBytes = QUEUED_MAX_BYTES,
                         queuedTotalMaxBytes = QUEUED_TOTAL_MAX_BYTES,
                         roomsMax = ROOMS_MAX, roomsMaxBytes = ROOMS_MAX_BYTES,
                         roomMaxBytes = ROOM_MAX_BYTES,
                         roomLimits = ROOM_LIMITS, joinLimits = JOIN_LIMITS,
                         peersMax = PEERS_PER_ROOM_MAX, limits = {},
                         updateLimits = UPDATE_LIMITS,
                         heapMaxBytes = v8.getHeapStatistics().heap_size_limit,
                         memoryMaxBytes = null } = {})
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
    const keyOf = clientOf(trustProxy, accounts.log);
    const api = accountRoutes(accounts, { corsOrigin, trustProxy,
                                          passkeys: keys, client: keyOf });

    const empties = new Map();
    const unwelcomed = new Map();       /* client -> sockets not welcomed */
    const costs = { bytes: 0 };
    const rates = { ...LIMITS, ...limits,
                    room: { ...RATES, ...limits.room } };
    const roomLimit = new TieredLimit(roomLimits, relayNow, () => {});
    const joinLimit = new TieredLimit(joinLimits, relayNow, () => {});
    const updateLimit = new TieredLimit(updateLimits, relayNow, () => {});
    const updatesIn = (client, bytes) =>
    {
        try
        {
            updateLimit.take(client, bytes);
            return true;
        }
        catch
        {
            return false;
        }
    };

    const forget = (r) =>
    {
        r.destroy();
        rooms.delete(r.name);
    };

    /* The rooms empty longest, other than `keep', that would have to go
       for the rooms to be charged `bytes' more and be `more' more, or
       null if all that are empty would not do. */
    const toLetGo = (bytes, more, keep) =>
    {
        const going = [];
        let left = rooms.size + more;
        let total = costs.bytes + bytes;

        for (const [old] of empties)
        {
            if (left <= roomsMax && total <= roomsMaxBytes)
                break;

            if (old !== keep)
            {
                going.push(old);
                left--;
                total -= old.bytes;
            }
        }

        return left <= roomsMax && total <= roomsMaxBytes ? going : null;
    };

    /* Whether room `r' may be charged `bytes' more, the rooms empty
       longest let go of to make room for it. */
    const spare = (r, bytes) =>
    {
        const going = toLetGo(bytes, 0, r);

        going?.forEach(forget);
        return going !== null;
    };

    /* A new room for `client', or `{ why, text, retryMs }' if it may not
       have one. Past the caps, the rooms empty longest go first to make
       room, and none if all that are empty would not; what a room costs
       is known once it is seeded, and before then the caps are looked
       at as if it cost what an empty one does, so that a relay too full
       for any room refuses one without reading a piece. One refused for
       the caps before it is made is not one of the client's new rooms;
       one made and seeded and then refused is, since that is most of what
       a room costs. */
    const newRoom = (name, seedWith, client) =>
    {
        const wait = roomLimit.waitMs(client);
        const full = { why: 'rooms',
                       text: 'the relay has as many rooms as it can hold; ' +
                             'join one already open, or try again later' };

        if (wait > 0)
            return { why: 'rooms', retryMs: wait,
                     text: 'too many new rooms from your address; try ' +
                           `again in ${Math.ceil(wait / 1000)} s` };

        if (toLetGo(ROOM_BYTES, 1, null) === null)
            return full;

        roomLimit.take(client);

        const r = new Room(name, seedWith, ctx);
        const going = toLetGo(0, 1, r);

        if (going === null)
        {
            r.destroy();
            return full;
        }

        going.forEach(forget);
        rooms.set(name, r);
        return r;
    };

    /* A room socket before its hello: nothing is made, or so much as
       looked up, for one the relay would not welcome. A page says nothing
       else first, and a socket that goes on saying something else is
       cut. */
    const greet = (ws, name, seedWith, client) =>
    {
        const deadline = setTimeout(() => ws.terminate(), HELLO_WITHIN_MS);
        const welcomed = () =>
        {
            const n = unwelcomed.get(client) - 1;

            clearTimeout(deadline);
            ws.off('close', welcomed);

            if (n === 0)
                unwelcomed.delete(client);
            else
                unwelcomed.set(client, n);
        };

        unwelcomed.set(client, (unwelcomed.get(client) ?? 0) + 1);
        ws.on('close', welcomed);

        const before = new Bucket(rates.room.other);
        const say = (m) => sendLine(queues, traffic, [ws], m);
        const hello = (data) =>
        {
            if (ws.readyState !== ws.OPEN)
                return;

            let m;

            try
            {
                m = JSON.parse(data.toString());
            }
            catch
            {
                m = null;
            }

            traffic?.roomIn(typeof m?.type === 'string' ? m.type : 'other',
                            data.length);

            if (m?.type !== 'hello')
            {
                if (!before.take())
                    ws.terminate();
                else if (m === null)
                    say({ type: 'error', text: 'not JSON' });
                else if (typeof m.type === 'string')
                    say({ type: 'error', text: 'hello first' });

                return;
            }

            /* Past its joins, nothing of the hello is looked up: a
               session or a name is a read of the accounts' file. */
            const wait = joinLimit.waitMs(client);
            const who = wait > 0 ? null
                : admit(m, corsOrigin === null ? null : accounts);
            const there = rooms.get(name);
            const again = there?.rejoining(m.was, who?.account) ?? null;

            /* A page joining again counts once: its old peer goes. It
               needs no invite and is not a removed one: the owner's
               removal closed its socket and took its tickets. */
            const r = wait > 0
                    ? { why: 'flood', retryMs: wait,
                        text: 'too many joins from your address; try ' +
                              `again in ${Math.ceil(wait / 1000)} s` }
                : who.text !== undefined ? who
                : there === undefined ? newRoom(name, seedWith, client)
                : again === null && who.account !== null &&
                      there.removed.has(who.account.id)
                    ? { why: 'removed',
                        text: 'the room\'s owner removed you from it' }
                : again === null && there.visibility === 'private' &&
                      m.invite !== there.invite && !there.ownedBy(who.account)
                    ? { why: 'private',
                        text: 'the room is private: join it by its invite ' +
                              'link' }
                : there.peers.size >= peersMax && again === null
                    ? { why: 'full',
                        text: `the room is full: ${peersMax} people are in it` }
                : there;

            if (r instanceof Room)
            {
                joinLimit.take(client);
                welcomed();
                ws.off('message', heard);
                r.attachRoom(ws, m, who, client);
            }
            else
            {
                say({ type: 'error', why: r.why, text: r.text,
                      retryMs: r.retryMs });
                ws.close();
            }
        };

        /* An exception here is the whole process. */
        const heard = (data) =>
        {
            try
            {
                hello(data);
            }
            catch (e)
            {
                process.stderr.write(`relay: hello failed: ${e.stack}\n`);
                ws.terminate();
            }
        };

        ws.on('message', heard);
        ws.on('error', () => ws.close());
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
                rooms: [...rooms].filter(([, r]) => r.visibility === 'public')
                    .map(([name, r]) =>
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
    const queues = new Queues(() => [roomWss.clients, docWss.clients],
                              { socketMax: queuedMaxBytes,
                                totalMax: queuedTotalMaxBytes });
    v8.setFlagsFromString('--expose-gc');

    const gc = vm.runInNewContext('gc');
    const shed = { rooms: 0, runs: 0, shedding: false, streak: 0 };
    let lastShare = 0;
    let settling = false;

    /* What the relay holds, as the larger share of the two it may. */
    const held = () =>
    {
        const { heapUsed, external } = process.memoryUsage();

        return Math.max(heapUsed / heapMaxBytes,
                        (heapUsed + external) / (memoryMaxBytes ?? Infinity));
    };

    const relieve = () =>
    {
        queues.trim();

        for (const r of rooms.values())
            [r.grew, r.lastBytes] = [r.bytes - r.lastBytes, r.bytes];

        if (held() < (shed.shedding ? SHED_LOW : SHED_HIGH))
        {
            shed.shedding = false;
            shed.streak = 0;
            settling = false;
            return;
        }

        gc();

        const share = held();
        const fell = share < lastShare;

        lastShare = share;
        shed.shedding = share >= SHED_LOW;

        if (!shed.shedding)
        {
            shed.streak = 0;
            settling = false;
            return;
        }

        const say = (what) => process.stderr.write(
            `relay: holding ${Math.round(100 * share)}% of its memory; ` +
            `shedding ${what}\n`);

        /* Before the lists are made: a room forgotten has no run. */
        if (empties.size > 0)
        {
            say(`the empty rooms (${empties.size})`);
            shed.rooms += empties.size;
            [...empties.keys()].forEach(forget);
        }

        if (settling)
        {
            settling = false;
            return;
        }

        const runs = [...rooms.values()].filter((r) => r.spareBytes > 0)
            .sort((a, b) => b.spareBytes - a.spareBytes);

        /* With what its sockets have queued, which it is not charged: a
           page that stops reading holds what it is sent until Queues.trim
           cuts it. */
        const weight = (r) => [...r.peers.values()].map((p) => p.ws)
            .concat([...r.docConns])
            .reduce((n, ws) => n + ws.bufferedAmount, r.bytes);
        const most = [...rooms.values()].filter((r) => !r.empty)
            .map((r) => [weight(r), r])
            .sort(([a, ra], [b, rb]) =>
                Math.max(rb.grew, 0) - Math.max(ra.grew, 0) || b - a)
            .map(([, r]) => r);

        if (runs.length + most.length === 0)
            return;

        for (let n = 2 ** shed.streak;
             n > 0 && runs.length + most.length > 0; n--)
            if (runs.length > 0)
            {
                const r = runs.shift();

                say(`room ${r.name}'s ${r.spareBytes} B kept for late ` +
                    'joiners');
                shed.runs++;
                r.overflow();
            }
            else if (most.length > 0)
            {
                const r = most.shift();

                say(`room ${r.name}, charged ${r.bytes} B, grown ` +
                    `${r.grew} B, and its ${r.peers.size} people`);
                shed.rooms++;
                r.destroy(SHORT_OF_MEMORY);
                rooms.delete(r.name);

                /* Its queues go now rather than at the closes, which a
                   page behind holds off for CUT_GRACE_MS. One that misses
                   the line for it takes the close as a drop, and joins
                   again all the same. */
                for (const p of r.peers.values())
                    p.ws.terminate();
            }

        if (!fell)
            shed.streak = Math.min(shed.streak + 1, SHED_DOUBLINGS_MAX);

        settling = true;
    };
    const trim = setInterval(relieve, QUEUE_CHECK_MS);
    const ctx = { tickets, ticketMs, tree, sessions: corsOrigin !== null,
                  traffic, queues, empties, costs, spare, limits: rates,
                  peersMax, bytesMax: roomMaxBytes, updatesIn };

    trim.unref();

    /* Nothing a client sends may throw out of here: an exception in an
       upgrade listener is the whole process. So is an error on a socket
       refused here, a reset after its 403, say: the server stops
       listening for its errors at the upgrade, and ws does only for the
       sockets it takes. */
    server.on('upgrade', (req, socket, head) =>
    {
        socket.on('error', () => socket.destroy());

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
           and only counts for the hello that makes the room. An empty one
           seeds nothing: a page joining again a room the relay has lost
           brings the document itself. */
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

        const client = keyOf(req);

        if (m[1] === 'room' &&
            (unwelcomed.get(client) ?? 0) >= UNWELCOMED_MAX)
        {
            socket.end('HTTP/1.1 429 Too Many Requests\r\n' +
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
                    greet(ws, m[2], seedWith, client);
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

    /* Empty rooms go after a while, and lapsed tickets with them. */
    const sweep = setInterval(() =>
    {
        for (const [t, { until }] of tickets)
            if (until <= relayNow())
                tickets.delete(t);

        for (const [r] of empties)
        {
            if (relayNow() - r.emptySince <= EMPTY_FOR_MS)
                break;

            forget(r);
        }
    }, SWEEP_EVERY_MS);

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
            rooms: rooms.size, roomBytes: costs.bytes, peers,
            roomSockets: roomWss.clients.size,
            docSockets: docWss.clients.size, awarenessClients,
            bufferedMaxBytes: [...roomWss.clients, ...docWss.clients]
                .reduce((max, ws) => Math.max(max, ws.bufferedAmount), 0),
            room: Object.fromEntries(traffic.room),
            doc: traffic.doc,
            catchups: traffic.catchups,
            catchupMaxBytes: traffic.catchupMaxBytes,
            shed,
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
        clearInterval(trim);

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
        metrics && new Promise((resolve, reject) =>
        {
            metrics.once('error', (e) => reject(
                new Error(`METRICS_PORT ${metricsPort}: ${e.message}`)));
            metrics.listen(metricsPort, '127.0.0.1', resolve);
        }),
    ]).then(() =>
    {
        server.rooms = rooms;
        server.accounts = accounts;
        server.metrics = metrics;
        return server;
    }, (e) =>
    {
        server.shutdown();
        throw e;
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
       127.0.0.1 to serve metrics on; MEMORY_MAX_BYTES the container's
       memory, which the relay sheds load short of (SHED_HIGH). */
    const opts = { db: process.env.DB || path.join(here, 'relay.db'),
                   corsOrigin: process.env.CORS_ORIGIN || null,
                   trustProxy: Number(process.env.TRUST_PROXY ?? 0),
                   metricsPort: process.env.METRICS_PORT
                       ? Number(process.env.METRICS_PORT) : null,
                   memoryMaxBytes: process.env.MEMORY_MAX_BYTES
                       ? Number(process.env.MEMORY_MAX_BYTES) : null };

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

    if (opts.memoryMaxBytes !== null &&
        !(Number.isSafeInteger(opts.memoryMaxBytes) && opts.memoryMaxBytes > 0))
    {
        process.stderr.write('relay.mjs: MEMORY_MAX_BYTES is a count of bytes\n');
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

    /* A metrics port asked for and not had is a misconfiguration: no
       relay, rather than one that cannot be seen into. */
    let server;

    try
    {
        server = await relay(opts);
    }
    catch (e)
    {
        process.stderr.write(`relay.mjs: ${e.message}\n`);
        process.exit(2);
    }

    const a = server.address();

    process.stdout.write(`relay on ws://${a.address}:${a.port}/  ` +
                         `(rooms seeded from ${path.resolve(
                             opts.tree ?? path.join(here, '..', '..'))}, ` +
                         `accounts in ${opts.db}` +
                         (server.metrics === null ? '' :
                          `, metrics on http://127.0.0.1:` +
                          `${server.metrics.address().port}/`) + ')\n');
}
