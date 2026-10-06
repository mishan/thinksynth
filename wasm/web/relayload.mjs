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
 * relayload.mjs -- a load on the relay, from peers made of the page's own
 * client code: room.js's Room, mesh.js's Mesh, y-websocket's provider let
 * in by the room socket's tickets, with ws for the WebSocket.
 *
 *   node wasm/web/relayload.mjs [options]
 *
 * Node has no WebRTC, so every Mesh link falls back to the relay, and
 * every gesture goes as `relayed' -- what the relay carries for a pair
 * whose ICE failed, and the most it is ever asked to carry. The senders
 * and receivers share one process and one clock, so a gesture's forward
 * latency is exact: its send time rides in it.
 *
 * With no --url it starts a relay of its own in this process, on port 0
 * with metrics on, and scrapes that. Those numbers include the peers'
 * own work on the same event loop: good for checking this tool, not for
 * sizing a relay.
 *
 * The peers' own process can be what gives out first. Its event loop's
 * delay is in the result beside the relay's; when it grows and the
 * relay's does not, a forward latency measures this tool. --shard spreads
 * one plan's peers over processes: each makes the same plan from the
 * same seed and --tag, and keeps its share of it. A shard's latencies and
 * counts are its own peers'; --merge adds the shards' results up, and
 * the first shard alone scrapes the relay.
 */

import fs from 'node:fs';
import path from 'node:path';
import { monitorEventLoopDelay } from 'node:perf_hooks';

import WebSocket from 'ws';
import * as Y from 'yjs';
import { WebsocketProvider } from 'y-websocket';

import { Maker, replayable } from './commands.js';
import { files, hashOf, pieceName, seenOf } from './doc.js';
import { Mesh } from './mesh.js';
import { Room } from './room.js';

/* room.js opens the global WebSocket; ws is the one the relay is
   written against. */
globalThis.WebSocket = WebSocket;

const PROFILES = ['idle', 'player', 'editor', 'chatter', 'late', 'flood',
                  'slow'];

const FLOODS = ['ping', 'signal', 'relayed', 'log'];

/* What a flood peer lets queue on its socket before it waits: as fast
   as the socket takes them, without holding them here. A socket that
   keeps up never fills, so a turn is also at most so many, and the peer
   still reads what comes back. A full one is looked at again a little
   later rather than on the next turn, which would spin this process. */
const FLOOD_QUEUE_MAX = 1024 * 1024;
const FLOOD_TURN_MAX = 256;
const FLOOD_WAIT_MS = 5;

const DELAY_RESOLUTION_MS = 10;

/* A scrape starts the relay's next window, and its event-loop timer runs
   every 10 ms: a window much shorter holds a few of its samples at most. */
const SCRAPE_EVERY_MIN_S = 0.1;

/* A distribution's buckets per factor of e: each a percent wide. */
const HIST_STEPS = 100;

/* Under the relay's chat rate (relay.mjs, CHAT_PER_SECOND). */
const CHAT_RATE_MAX = 4;

const USAGE = `usage: relayload.mjs [options]

  --url ws://HOST:PORT   the relay; without it, one started here (local)
  --metrics URL          its metrics port to scrape (local: its own)
  --scrape-every S       seconds between scrapes (1)
  --rooms N              rooms (1)
  --peers N              peers per room (4)
  --mix P=W,...          profiles by weight, from idle, player, editor,
                         chatter, late, flood, slow
                         (player=2,editor=1,chatter=1,late=0)
  --rate R               a player's knob moves per second (10)
  --edit-rate R          an editor's edits per second (2)
  --chat-rate R          a chatter's lines per second (0.5, at most ${CHAT_RATE_MAX})
  --transport-every S    seconds between a room's restarts (4; 0 none)
  --late-after S         seconds before a late peer joins (duration / 3)
  --flood TYPE           what a flood peer sends as fast as its socket
                         takes it: ping, signal, relayed or log (relayed)
  --pad N                bytes of filler in every knob move (0)
  --duration S           seconds of load, ramp included (10)
  --ramp S               seconds over which peers join (0)
  --seed N               for every random choice (1)
  --piece NAME.gen       what a new room is seeded with (the relay's default)
  --tag NAME             the rooms' names start with this (from the seed
                         and the time)
  --shard I/K            the I'th of K processes, which keeps every K'th
                         peer of the plan; all K need the same --tag and
                         --url, and only the 0th scrapes
  --out FILE             the JSON result

  relayload.mjs --merge FILE.json... [--out FILE]
                         the shards' results added up
`;

function parseArgs (argv)
{
    const o = { url: null, metrics: null, scrapeEvery: 1, rooms: 1, peers: 4,
                mix: 'player=2,editor=1,chatter=1,late=0', rate: 10,
                editRate: 2, chatRate: 0.5, transportEvery: 4,
                lateAfter: null, flood: 'relayed', pad: 0, duration: 10,
                ramp: 0, seed: 1, piece: null, tag: null, shard: '0/1',
                out: null };
    const any = ['a number, 0 or more', (x) => x >= 0];
    const whole = ['a whole number', (x) => Number.isInteger(x) && x >= 0];
    const count = ['a whole number, 1 or more',
                   (x) => Number.isInteger(x) && x >= 1];
    const numbers = {
        '--scrape-every': ['scrapeEvery',
                           `at least ${SCRAPE_EVERY_MIN_S} seconds`,
                           (x) => x >= SCRAPE_EVERY_MIN_S],
        '--rooms': ['rooms', ...count], '--peers': ['peers', ...count],
        '--rate': ['rate', ...any], '--edit-rate': ['editRate', ...any],
        '--chat-rate': ['chatRate', ...any],
        '--transport-every': ['transportEvery', ...any],
        '--late-after': ['lateAfter', ...any], '--pad': ['pad', ...whole],
        '--duration': ['duration', 'a number of seconds above 0',
                       (x) => x > 0],
        '--ramp': ['ramp', ...any], '--seed': ['seed', ...whole] };
    const strings = { '--url': 'url', '--metrics': 'metrics', '--mix': 'mix',
                      '--piece': 'piece', '--flood': 'flood', '--tag': 'tag',
                      '--shard': 'shard', '--out': 'out' };

    for (let i = 0; i < argv.length; i++)
    {
        const a = argv[i];

        if (a === '--local')
            o.url = null;
        else if ((a in numbers || a in strings) && i + 1 < argv.length)
        {
            const v = argv[++i];

            if (a in strings)
                o[strings[a]] = v;
            else
            {
                const [key, what, ok] = numbers[a];

                if (!ok(Number(v)))
                    throw new Error(`${a} takes ${what}, not ${v}`);

                o[key] = Number(v);
            }
        }
        else
            throw new Error(a === '--help' ? '' : `what is ${a}?`);
    }

    o.chatRate = Math.min(o.chatRate, CHAT_RATE_MAX);

    if (!FLOODS.includes(o.flood))
        throw new Error(`--flood: ${o.flood}?`);

    const shard = /^(\d+)\/(\d+)$/.exec(o.shard);

    if (shard === null || !(Number(shard[1]) < Number(shard[2])))
        throw new Error(`--shard: ${o.shard}?`);

    o.shardOf = [Number(shard[1]), Number(shard[2])];

    /* Shards each making rooms of their own, or each a relay of their
       own, would be K runs and not one. */
    if (o.shardOf[1] > 1 && (o.tag === null || o.url === null))
        throw new Error('--shard needs --tag and --url, the same for all');

    o.lateAfter ??= o.duration / 3;
    o.weights = {};

    for (const part of o.mix.split(','))
    {
        const [p, w = '1'] = part.split('=');

        if (!PROFILES.includes(p) || !(Number(w) >= 0))
            throw new Error(`--mix: ${part}?`);

        o.weights[p] = Number(w);
    }

    return o;
}

/* mulberry32: one stream of numbers from one seed, so a run can be made
   again. */
function prng (seed)
{
    let a = seed >>> 0;

    return () =>
    {
        a = (a + 0x6D2B79F5) >>> 0;

        let t = a;

        t = Math.imul(t ^ (t >>> 15), t | 1);
        t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
        return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
}

/* A room's peers by profile, in proportion to the weights: the largest
   remainders get the peers the whole parts leave over, and among equal
   ones the `r'th room starts `r' along, so that between them the rooms
   have every profile. A profile weighed 0 gets none. */
function profilesOf (weights, n, r)
{
    const total = Object.values(weights).reduce((s, w) => s + w, 0);
    const share = Object.entries(weights).filter(([, w]) => w > 0)
        .map(([p, w], i, all) =>
        {
            const exact = n * w / total;

            /* Rounded, so that thirds and the like tie. */
            return { p, exact, left: Math.round((exact % 1) * 1e9),
                     turn: (i - r % all.length + all.length) % all.length };
        });
    const out = [];

    for (const s of share)
        for (let i = 0; i < Math.floor(s.exact); i++)
            out.push(s.p);

    share.sort((a, b) => b.left - a.left || a.turn - b.turn);

    for (let i = 0; out.length < n; i++)
        out.push(total > 0 ? share[i].p : 'idle');

    return out;
}

/* A distribution as counts in buckets a percent wide, which the shards'
   add up, and which holds a long run in a few hundred numbers. The
   count, the mean and the extremes are exact, the percentiles to the
   bucket. Nothing above 0 is in the `0' bucket. */
class Hist
{
    constructor ({ n = 0, sum = 0, min = null, max = null, buckets = {} } = {})
    {
        Object.assign(this, { n, sum, min, max, buckets: { ...buckets } });
    }

    add (x)
    {
        const k = x > 0 ? `e${Math.round(Math.log(x) * HIST_STEPS)}` : '0';

        this.buckets[k] = (this.buckets[k] ?? 0) + 1;
        this.n++;
        this.sum += x;
        this.min = Math.min(this.min ?? x, x);
        this.max = Math.max(this.max ?? x, x);
    }

    merge (h)
    {
        for (const [k, c] of Object.entries(h.buckets))
            this.buckets[k] = (this.buckets[k] ?? 0) + c;

        this.n += h.n;
        this.sum += h.sum;

        if (h.n > 0)
        {
            this.min = Math.min(this.min ?? h.min, h.min);
            this.max = Math.max(this.max ?? h.max, h.max);
        }
    }

    stats ()
    {
        if (this.n === 0)
            return { n: 0 };

        const bs = Object.entries(this.buckets)
            .map(([k, c]) => [k === '0' ? 0 : Math.exp(k.slice(1) / HIST_STEPS),
                              c])
            .sort((a, b) => a[0] - b[0]);
        const r = (x) => Math.round(x * 1000) / 1000;
        const q = (f) =>
        {
            const at = Math.min(this.n - 1, Math.floor(f * this.n));
            let below = 0;

            for (const [x, c] of bs)
            {
                below += c;

                if (below > at)
                    return r(Math.min(this.max, Math.max(this.min, x)));
            }
        };

        return { n: this.n, p50: q(0.5), p99: q(0.99), p999: q(0.999),
                 max: r(this.max), mean: r(this.sum / this.n) };
    }
}

/* Wall-clock milliseconds, which every process on the machine reads
   alike: a gesture sent by one shard is received by the others. */
const now = () => performance.timeOrigin + performance.now();

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

class Tally
{
    constructor ()
    {
        this.counts = {};
    }

    add (what)
    {
        this.counts[what] = (this.counts[what] ?? 0) + 1;
    }
}

const HISTS = ['joinMs', 'welcomeMs', 'syncMs', 'forwardMs', 'rttMs',
               'spreadMs', 'catchupMs', 'catchupBytes', 'catchupLog'];

/* What every peer adds to: one process, one clock. */
const results = {
    ...Object.fromEntries(HISTS.map((h) => [h, new Hist()])),
    sent: 0, expected: 0, unready: 0,
    offsets: new Map(),                         /* room -> [min, max] */
    edits: 0, chats: 0, knobs: 0, transports: 0,
    flood: { sent: 0, bytes: 0 },
    errors: new Tally(), closes: new Tally(), refusals: new Tally(),
};

let stopping = false;

/* One fake page. */
class Peer
{
    constructor (o, url, roomName, name, profile, rand, conductor)
    {
        Object.assign(this, { o, url, roomName, name, profile, rand,
                              conductor });
        this.timers = [];
        this.room = null;
        this.provider = null;
        this.doc = null;
    }

    async join ()
    {
        const t0 = performance.now();

        /* On the wall clock, so that offsets agree across shards. */
        this.room = new Room(this.url, this.roomName, this.name,
                             { piece: this.o.piece, now });
        this.room.on('error', (text) => results.errors.add(`relay: ${text}`))
            .on('refused', (m) => results.refusals.add(`${m.of}: ${m.why}`))
            /* Its sender counts this peer from `joined', but its Mesh,
               which takes this over, is made only once the doc is
               synced. */
            .on('relayed', (from, cmd) =>
            {
                if (typeof cmd?.sentMs === 'number')
                    results.unready++;
            })
            .on('close', (refused) =>
            {
                if (!stopping)
                    results.closes.add(`room: ${refused?.why ?? 'closed'}`);
            })
            .on('clock', () =>
            {
                /* A flood of pings is answered behind its own backlog. */
                if (this.profile !== 'flood')
                    results.rttMs.add(this.room.clock.samples.at(-1).rtt);
            });

        try
        {
            await this.room.connect();
        }
        catch (e)
        {
            results.errors.add(`join: ${e.why ?? e.message}`);
            return false;
        }

        const tWelcome = performance.now();

        this.doc = new Y.Doc();
        this.provider = new WebsocketProvider(
            `${this.url}/doc`, this.roomName, this.doc,
            { WebSocketPolyfill: WebSocket, params: { ticket: this.room.ticket },
              /* Node has a BroadcastChannel, which would carry one
                 provider's updates to the others here past the relay. */
              disableBc: true });

        /* As jam.js's followTickets: the next reconnect goes in with the
           latest ticket. */
        this.room.on('ticket', (ticket) => { this.provider.params = { ticket }; });
        this.provider.on('connection-error', () =>
            results.errors.add('doc: connection error'));
        this.provider.on('connection-close', (e) =>
        {
            if (!stopping)
                results.closes.add(`doc: ${e?.code ?? 'closed'}`);
        });
        this.provider.awareness.setLocalStateField('user', { name: this.name });

        await new Promise((resolve) => this.provider.synced
            ? resolve() : this.provider.once('synced', resolve));

        const tSynced = performance.now();

        results.welcomeMs.add(tWelcome - t0);
        results.syncMs.add(tSynced - tWelcome);
        results.joinMs.add(tSynced - t0);

        this.mesh = new Mesh(this.room, (from, cmd) => this.received(cmd));
        this.maker = new Maker(this.room.peer, () => this.transportNow());
        return true;
    }

    transportNow ()
    {
        const p = this.room.playing;

        return p === null ? -1 : (this.room.relayNow() - p.origin) / 1000;
    }

    received (cmd)
    {
        if (typeof cmd?.sentMs === 'number')
            results.forwardMs.add(now() - cmd.sentMs);
    }

    /* As jam.js's send: by the mesh, and a start or a stop by the room
       socket too, and any other stamped command's copy for the log. */
    send (cmd)
    {
        if (cmd.type === 'knob')
        {
            /* Every link's, other shards' peers too: what all of them
               deliver between them. */
            cmd.sentMs = now();
            results.sent++;
            results.expected += this.mesh.links.size;
        }

        this.mesh.broadcast(cmd);

        if (cmd.type === 'transport')
            this.room.transport(cmd);
        else if (replayable(cmd))
            this.room.log(cmd);
    }

    async play ()
    {
        /* An origin is in relay time, which the first pong gives. */
        while (this.room.clock.count === 0 && !stopping)
            await sleep(20);

        const origin = this.room.relayNow() + this.maker.transportLead * 1000;

        this.send(this.maker.start(origin, await hashOf(this.doc),
                                   Math.floor(this.rand() * 0x100000000), 0,
                                   seenOf(this.doc)));
        results.transports++;
    }

    every (rate, fn)
    {
        if (!(rate > 0))
            return;

        const ms = 1000 / rate;

        /* Out of step with the other peers, as people are. */
        this.timers.push(setTimeout(() =>
        {
            if (stopping)
                return;

            fn();
            this.timers.push(setInterval(() => { if (!stopping) fn(); }, ms));
        }, this.rand() * ms));
    }

    async run ()
    {
        const { o, rand } = this;

        switch (this.profile)
        {
            case 'player':
                if (this.conductor)
                {
                    await this.play();

                    if (o.transportEvery > 0)
                        this.every(1 / o.transportEvery, async () =>
                        {
                            this.send({ ...this.maker.stop(),
                                        run: this.room.runKey });
                            await this.play();
                            results.transports++;
                        });
                }

                this.every(o.rate, () =>
                {
                    const cmd = this.maker.knob(`knob${Math.floor(rand() * 8)}`,
                                                rand());

                    if (o.pad > 0)
                        cmd.pad = 'x'.repeat(o.pad);

                    this.send(cmd);
                    results.knobs++;
                });
                break;

            case 'flood':
                this.flood();
                break;

            /* Stops reading its room socket: what the relay sends it
               piles up. */
            case 'slow':
                this.room.ws._socket.pause();
                break;

            case 'editor':
            {
                const line = '# load\n';
                let typed = null;
                let at = 0;

                this.every(o.editRate, () =>
                {
                    const text = files(this.doc).get(pieceName(this.doc));

                    if (text === undefined)
                        return;

                    /* Typing and taking it back, so the text stays the
                       size it was. Each character is taken back from
                       wherever the other editors' typing has moved it
                       since, and only those: another's line typed into
                       the middle of this one stays. */
                    if (typed === null)
                    {
                        at = Math.floor(rand() * (text.length + 1));
                        text.insert(at, line);
                        typed = [...line].map((c, i) =>
                            Y.createRelativePositionFromTypeIndex(text, at + i));
                    }
                    else
                    {
                        this.doc.transact(() =>
                        {
                            for (const rel of typed)
                            {
                                const { index } = Y
                                    .createAbsolutePositionFromRelativePosition(
                                        rel, this.doc);

                                text.delete(index, 1);
                            }
                        });
                        typed = null;
                        at = 0;
                    }

                    this.provider.awareness.setLocalStateField(
                        'cursor', { at });
                    results.edits++;
                });
                break;
            }

            case 'chatter':
            {
                let n = 0;

                this.every(o.chatRate, () =>
                {
                    this.room.chat(`line ${++n} from ${this.name}`, n);
                    results.chats++;
                });
                break;
            }

            case 'late':
            {
                const t0 = performance.now();
                let m;

                try
                {
                    m = await this.room.catchUp();
                }
                catch (e)
                {
                    results.errors.add(`catchup: ${e.message}`);
                    break;
                }

                results.catchupMs.add(performance.now() - t0);
                results.catchupBytes.add(Buffer.byteLength(JSON.stringify(m)));
                results.catchupLog.add(m.log?.length ?? 0);
                break;
            }
        }
    }

    /* One message after another, each in the shape a page's would be, for
       as long as the socket keeps up with them. */
    flood ()
    {
        const { ws } = this.room;
        const to = [...this.mesh.links.keys()][0] ?? 'nobody';
        let n = 0;
        const make = {
            ping: () => ({ type: 'ping', t0: this.room.now() }),
            signal: () => ({ type: 'signal', to,
                             data: { candidate: { candidate: `flood ${n}` } } }),
            relayed: () => ({ type: 'relayed',
                              data: this.maker.knob('knob0', this.rand()) }),
            log: () => ({ type: 'log', data: this.maker.knob('knob0',
                                                             this.rand()),
                          run: this.room.runKey }),
        }[this.o.flood];

        const burst = () =>
        {
            if (stopping || ws.readyState !== WebSocket.OPEN)
                return;

            if (ws.bufferedAmount >= FLOOD_QUEUE_MAX)
            {
                setTimeout(burst, FLOOD_WAIT_MS);
                return;
            }

            for (let k = 0; k < FLOOD_TURN_MAX &&
                            ws.bufferedAmount < FLOOD_QUEUE_MAX; k++)
            {
                const line = JSON.stringify(make());

                ws.send(line);
                n++;
                results.flood.sent++;
                results.flood.bytes += line.length;
            }

            setImmediate(burst);
        };

        burst();
    }

    clockAtEnd ()
    {
        const c = this.room?.clock;

        if (!(c?.count > 0) || this.profile === 'flood')
            return;

        const [lo, hi] = results.offsets.get(this.roomName) ??
                         [c.offset, c.offset];

        results.spreadMs.add(c.spread);
        results.offsets.set(this.roomName, [Math.min(lo, c.offset),
                                            Math.max(hi, c.offset)]);
    }

    close ()
    {
        for (const t of this.timers)
            clearInterval(t);

        this.mesh?.close();
        this.room?.close();

        if (this.provider !== null)
        {
            this.provider.destroy();
            this.provider.awareness.destroy();
            this.doc.destroy();
        }
    }
}

async function main ()
{
    let o;

    if (process.argv[2] === '--merge')
    {
        merge(process.argv.slice(3));
        return;
    }

    try
    {
        o = parseArgs(process.argv.slice(2));
    }
    catch (e)
    {
        process.stderr.write(`${e.message ? `relayload.mjs: ${e.message}\n`
                                          : ''}${USAGE}`);
        process.exit(2);
    }

    const rand = prng(o.seed);
    const local = o.url === null;
    let server = null;
    let url = o.url;
    /* A scrape starts the relay's next window: one scraper. */
    let metricsUrl = o.shardOf[0] === 0 ? o.metrics : null;

    if (local)
    {
        const { relay } = await import('./relay.mjs');

        server = await relay({ port: 0, host: '127.0.0.1', metricsPort: 0 });
        url = `ws://127.0.0.1:${server.address().port}`;
        metricsUrl ??= `http://127.0.0.1:${server.metrics.address().port}/`;
    }

    /* A run's rooms are its own, on a relay others may be using. */
    const tag = o.tag ?? `load-${(o.seed >>> 0).toString(36)}-` +
                         `${Date.now().toString(36).slice(-4)}`;
    const peers = [];

    for (let r = 0; r < o.rooms; r++)
    {
        const kinds = profilesOf(o.weights, o.peers, r);
        const first = kinds.indexOf('player');

        /* `guest-' starts no account's handle, so a relay with accounts
           takes these names as guests' too. */
        kinds.forEach((profile, p) => peers.push(new Peer(
            o, url, `${tag}-${r}`, `guest-load-${r}-${p}`, profile,
            prng(o.seed * 7919 + r * 131 + p), p === first)));
    }

    /* Each provider listens for the process's exit, as a page's would for
       its own. */
    process.setMaxListeners(peers.length + 10);

    const series = [];
    const t0 = performance.now();
    const scrape = async () =>
    {
        const at = Math.round(performance.now() - t0);

        try
        {
            series.push({ atMs: at, ...await (await fetch(
                new URL('?reset', metricsUrl))).json() });
        }
        catch (e)
        {
            series.push({ atMs: at, error: e.message });
        }
    };
    /* One at a time: a busy relay answers late, and each answer starts
       its event-loop window again. */
    let scraping = null;
    const scraper = metricsUrl === null ? null : setInterval(() =>
    {
        scraping ??= scrape().finally(() => { scraping = null; });
    }, o.scrapeEvery * 1000);

    if (metricsUrl !== null)
        await scrape();

    /* Room-major order interleaved, so every room fills over the ramp
       rather than one after another. */
    const order = [...peers.keys()].sort((a, b) =>
        (a % o.peers) - (b % o.peers) || a - b);
    const [shard, shards] = o.shardOf;
    const mine = new Set(peers.filter((p, k) => k % shards === shard));
    const delay = monitorEventLoopDelay({ resolution: DELAY_RESOLUTION_MS });
    const cpu0 = process.cpuUsage();

    delay.enable();

    const joins = order.map(async (k, i) =>
    {
        const peer = peers[k];
        const at = (o.ramp * 1000 * i) / peers.length +
                   (peer.profile === 'late' ? o.lateAfter * 1000 : 0);

        if (!mine.has(peer))
            return;

        await sleep(at);

        if (stopping || !await peer.join() || stopping)
            return;

        await peer.run();
    });

    await Promise.race([Promise.all(joins), sleep(o.duration * 1000)]);
    await sleep(Math.max(0, o.duration * 1000 - (performance.now() - t0)));

    stopping = true;
    delay.disable();

    const cpu = process.cpuUsage(cpu0);
    const pastResolution = (ns) =>
        Math.round(Math.max(0, ns / 1e6 - DELAY_RESOLUTION_MS) * 1000) / 1000;

    /* What is in flight lands. */
    await sleep(500);

    for (const p of mine)
        p.clockAtEnd();

    if (scraper !== null)
    {
        clearInterval(scraper);
        await scraping;
        await scrape();
    }

    for (const p of mine)
        p.close();

    await sleep(200);
    server?.shutdown();

    const out = {
        inputs: { ...o, url: local ? null : url, local, metrics: metricsUrl,
                  tag },
        totals: {
            peers: { planned: mine.size,
                     byProfile: Object.fromEntries(PROFILES.map((p) =>
                         [p, [...mine].filter((x) => x.profile === p)
                             .length])) },
            hists: Object.fromEntries(HISTS.map((h) => [h, results[h]])),
            offsets: Object.fromEntries(results.offsets),
            gestures: { sent: results.sent, expected: results.expected,
                        unready: results.unready },
            sent: { knobs: results.knobs, edits: results.edits,
                    chats: results.chats, transports: results.transports },
            flood: results.flood,
            errors: results.errors.counts,
            closes: results.closes.counts,
            refusals: results.refusals.counts,
            generators: [{
                eventLoopDelayMs: { p50: pastResolution(delay.percentile(50)),
                                    p99: pastResolution(delay.percentile(99)),
                                    max: pastResolution(delay.max) },
                cpuPercent: Math.round((cpu.user + cpu.system) /
                                       (o.duration * 1e4)),
            }],
        },
        metrics: series,
    };

    write(o.out, out);

    /* A join still under way at the end opens its sockets after the
       others were closed, and would reconnect for ever. */
    process.exit(0);
}

/* The result of a run, or of shards' runs added up, and printed. */
function write (file, out)
{
    out.results = report(out.totals);

    if (file !== null)
    {
        fs.mkdirSync(path.dirname(path.resolve(file)), { recursive: true });
        fs.writeFileSync(file, JSON.stringify(out, null, 2) + '\n');
    }

    process.stdout.write(summary(out));
}

/* --merge: the shards of one run, as one. Their offsets are on one wall
   clock, so a room's range spans them; the relay's metrics are the
   scraping shard's. */
function merge (argv)
{
    const out = argv.indexOf('--out');
    const file = out < 0 ? null : argv[out + 1];
    const runs = argv
        .filter((a, i) => out < 0 || (i !== out && i !== out + 1))
        .map((f) => JSON.parse(fs.readFileSync(f, 'utf8')));
    const add = (into, from) =>
    {
        for (const [k, v] of Object.entries(from))
            into[k] = (into[k] ?? 0) + v;
    };
    const t = { peers: { planned: 0, byProfile: {} }, hists: {}, offsets: {},
                gestures: {}, sent: {}, flood: {}, errors: {}, closes: {},
                refusals: {}, generators: [] };

    if (runs.length === 0 || file === undefined ||
        new Set(runs.map((r) => r.inputs.tag)).size > 1)
    {
        process.stderr.write('relayload.mjs: --merge takes the result files ' +
                             'of one run, the same --tag\n' + USAGE);
        process.exit(2);
    }

    for (const { totals: x } of runs)
    {
        t.peers.planned += x.peers.planned;
        add(t.peers.byProfile, x.peers.byProfile);

        for (const [h, v] of Object.entries(x.hists))
            (t.hists[h] ??= new Hist()).merge(v);

        for (const [room, [lo, hi]] of Object.entries(x.offsets))
        {
            const [a, b] = t.offsets[room] ?? [lo, hi];

            t.offsets[room] = [Math.min(a, lo), Math.max(b, hi)];
        }

        for (const k of ['gestures', 'sent', 'flood', 'errors', 'closes',
                         'refusals'])
            add(t[k], x[k]);

        t.generators.push(...x.generators);
    }

    write(file, {
        inputs: { ...runs[0].inputs, shard: `${runs.length} merged` },
        totals: t,
        metrics: runs.find((r) => r.metrics.length > 0)?.metrics ?? [],
    });
}

/* What `totals' come to. The generator's is the worst shard's. */
function report (t)
{
    const h = Object.fromEntries(HISTS.map((k) => [k, new Hist(t.hists[k])]));
    const range = new Hist();
    const worst = (f) => Math.max(...t.generators.map(f));

    for (const [lo, hi] of Object.values(t.offsets))
        range.add(hi - lo);

    return {
        peers: { planned: t.peers.planned, joined: h.joinMs.n,
                 byProfile: t.peers.byProfile },
        generator: {
            eventLoopDelayMs: { p50: worst((g) => g.eventLoopDelayMs.p50),
                                p99: worst((g) => g.eventLoopDelayMs.p99),
                                max: worst((g) => g.eventLoopDelayMs.max) },
            cpuPercent: worst((g) => g.cpuPercent),
        },
        joinMs: h.joinMs.stats(),
        welcomeMs: h.welcomeMs.stats(),
        syncMs: h.syncMs.stats(),
        forwardMs: h.forwardMs.stats(),
        gestures: { ...t.gestures, delivered: h.forwardMs.n },
        sent: t.sent,
        flood: t.flood,
        clock: { rttMs: h.rttMs.stats(),
                 spreadMs: h.spreadMs.stats(),
                 /* Every peer reads the same wall clock, so a room's
                    offsets would agree if each were exact. */
                 offsetRangeMs: range.stats() },
        catchup: { ms: h.catchupMs.stats(),
                   bytes: h.catchupBytes.stats(),
                   log: h.catchupLog.stats() },
        errors: t.errors,
        closes: t.closes,
        refusals: t.refusals,
    };
}
function summary ({ inputs, results: r, metrics })
{
    const f = (s) => s.n === 0 ? '-' : `p50 ${s.p50} p99 ${s.p99} ` +
                                       `p99.9 ${s.p999} max ${s.max}`;
    const counts = (c) => Object.keys(c).length === 0 ? 'none'
        : Object.entries(c).map(([k, v]) => `${k} x${v}`).join(', ');
    /* The first scrape's window is from before the run, and its counters
       are the relay's since it started: the run is what came after. */
    const ok = metrics.filter((m) => m.error === undefined);
    const [first, last] = [ok[0], ok.at(-1)];
    const windows = ok.slice(1);
    const delays = windows.map((m) => m.eventLoopDelayMs);
    const lines = [
        `relayload: ${inputs.rooms} room(s) x ${inputs.peers} peers, ` +
        `${inputs.duration} s, ${inputs.mix}, seed ${inputs.seed}` +
        (inputs.local ? ' (local relay, same process)' : ` at ${inputs.url}`),
        `  peers joined   ${r.peers.joined}/${r.peers.planned} ` +
        `(${counts(r.peers.byProfile)})`,
        `  join ms        ${f(r.joinMs)}  (welcome ${r.welcomeMs.p50 ?? '-'}, ` +
        `sync ${r.syncMs.p50 ?? '-'} at p50)`,
        `  forward ms     ${f(r.forwardMs)}`,
        `  generator      event loop p99 ${r.generator.eventLoopDelayMs.p99} ` +
        `max ${r.generator.eventLoopDelayMs.max} ms; ` +
        `cpu ${r.generator.cpuPercent}%`,
        `  gestures       ${r.gestures.delivered}/${r.gestures.expected} ` +
        `delivered of ${r.gestures.sent} sent, ${r.gestures.unready} ` +
        'before the receiver was ready' +
        (inputs.shardOf[1] > 1 && !inputs.shard.endsWith('merged')
            ? ' (expected counts other shards\' peers; --merge them)' : ''),
        `  sent           ${counts(r.sent)}`,
        `  ping rtt ms    ${f(r.clock.rttMs)}`,
        `  clock spread   ${f(r.clock.spreadMs)}; offsets across peers ` +
        `${f(r.clock.offsetRangeMs)}`,
        `  catch-up       ms ${f(r.catchup.ms)}; bytes ${f(r.catchup.bytes)}`,
        `  errors         ${counts(r.errors)}`,
        `  closes         ${counts(r.closes)}`,
        `  refusals       ${counts(r.refusals)}`,
    ];

    if (windows.length > 0)
        lines.push(
            `  relay          ${metrics.length} scrapes; event loop max ms ` +
            `${Math.max(...delays.map((d) => d.max)).toFixed(1)}, worst p99 ` +
            `${Math.max(...delays.map((d) => d.p99)).toFixed(1)}, timer ` +
            `lag max ${Math.max(...windows.map((m) => m.timerLagMs.max))
                .toFixed(1)}, cpu max ${Math.max(...windows.map(
                    (m) => m.cpuPercent)).toFixed(0)}%; rss ` +
            `${(last.memoryBytes.rss / 2 ** 20).toFixed(0)} MiB; relayed ` +
            `in/out ${last.room.relayed.in - first.room.relayed.in}/` +
            `${last.room.relayed.out - first.room.relayed.out}; doc frames ` +
            `in/out ${last.doc.in - first.doc.in}/` +
            `${last.doc.out - first.doc.out}; ` +
            `buffered max ${last.bufferedMaxBytes} B`);

    return lines.join('\n') + '\n';
}

await main();
