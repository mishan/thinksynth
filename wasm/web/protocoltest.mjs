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
 * protocoltest.mjs -- two peers in one process, on a network made of
 * delay, jitter and loss, composing one piece: is it one tape?
 *
 *   node wasm/web/protocoltest.mjs [BUILD_DIR] [NODE_BUILD_DIR]
 *
 * M3's first gate (JAM_M3.md, section 8.1), and the "two schedulers in one
 * process" docs/JAM.md's section 5 asks for before any of this gets a UI. No
 * browser, no relay, no sockets: a simulation whose wall clock is a number,
 * with a relay whose clock is another number, and two peers each holding
 * the browser module -- the same wasm the worklet runs -- stepped at a
 * window and a rate of its own: 256 at 48 kHz for one, 1024 at 44.1 kHz for
 * the other, their blocks out of phase. Between them, the modules the page
 * itself uses: clock.js makes the maps, commands.js makes and applies the
 * commands.
 *
 * A script presses Play on one peer, moves a knob from each side at times
 * of its own, changes the tempo from each side, and stops. Every command
 * is stamped with the transport time it applies at and sent ahead of it,
 * and every peer, the sender included, applies it at that time inside the
 * step. So the two tapes must be one tape -- and both must be the tape
 * genwav.mjs delivers under Node from the same piece and the same command
 * stream, stepped in windows of 1024 from transport zero with no origin
 * and no network at all. That is property 4 of JAM_M3.md section 2, and
 * with it property 1: a command applied at a window boundary rather than at
 * its stamp would agree on one machine and part on two.
 *
 * Then the second half: the same script with the delay raised past the
 * knob lead. Now a knob reaches the other peer after its time, that peer
 * applies it late and counts it, the tapes part, and the harness has to
 * see both -- the count, and which command. A hazard that is hidden is
 * worse than one that is shown.
 *
 * Exit status is the number of failures.
 */

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

import { drain, tapeBefore, tapeLine } from '../tape.mjs';
import { AudioClock, RelayClock, TransportClock, frameOfRelayMs }
    from './clock.js';
import { Dedupe, KNOB_LEAD, Maker, TRANSPORT_LEAD, apply, isLate }
    from './commands.js';
import { firstDifference, instruments, pieces, reference }
    from './piececheck.mjs';
import { loadPiece, schedule } from './render.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const top = path.join(here, '..', '..');
const build = path.resolve(process.argv[2] ?? path.join(top, 'build-web'));
const nodeBuild = path.resolve(process.argv[3] ??
                               process.env.THINK_WASM_BUILD ??
                               path.join(top, 'build-wasm'));

/* How long the transport runs, in transport seconds. */
const SECONDS = 30;

/* The two seats. Different rates, different windows, and blocks that do
   not line up with each other or with anything: the case the stamp is
   for.
 *
   `startFrame' is where each context's frame counter already is by the
   time its module is made: a browser's has been running since the click
   that made the AudioContext, through the fetch and the instantiate, and
   the two peers spend different amounts of time on that. The module
   counts from zero and is told where that is (thinkweb.cpp, tw_align);
   with these left at zero the harness could not tell whether it had
   been. */
const PEERS = [
    { name: 'A', rate: 48000, windowlen: 256, block: 128, phase: 0.0,
      perfOffset: 1000, startFrame: 16768 },
    { name: 'B', rate: 44100, windowlen: 1024, block: 1024, phase: 7.3,
      perfOffset: 250000, startFrame: 52224 },
];

/* The relay's clock against the simulation's, in milliseconds: nothing a
   peer can see except through a ping. */
const RELAY_OFFSET = 987654.321;

/* Two networks. The first is a LAN with something on it; the second is
   the one that breaks the knob lead. */
const NETWORKS = {
    lan:  { delay: 40, jitter: 20, loss: 0.02 },
    slow: { delay: 300, jitter: 20, loss: 0.0 },
};

/* mulberry32: a small seeded generator, so a run is a run. */
function rng (seed)
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

/* The simulation: a clock in milliseconds and things that happen on it,
   in order. */
class Sim
{
    constructor ()
    {
        this.t = 0;
        this.queue = [];
        this.seq = 0;
    }

    at (delay, fn)
    {
        const e = { t: this.t + delay, seq: this.seq++, fn };
        let k = this.queue.length;

        while (k > 0 && (this.queue[k - 1].t > e.t ||
                         (this.queue[k - 1].t === e.t &&
                          this.queue[k - 1].seq > e.seq)))
            k--;

        this.queue.splice(k, 0, e);
    }

    /* Runs until `done' says so or nothing is left. An event may be
       asynchronous -- applying a start awaits the piece load, as it does
       on the page, where the load is a round trip to the worklet -- and
       the clock does not move until it has finished. */
    async run (done)
    {
        while (this.queue.length > 0 && !done())
        {
            const e = this.queue.shift();

            this.t = e.t;
            await e.fn();
        }
    }
}

/* A network between the peers: a delay, some jitter, some loss. */
class Net
{
    constructor (sim, { delay, jitter, loss }, seed)
    {
        this.sim = sim;
        this.delay = delay;
        this.jitter = jitter;
        this.loss = loss;
        this.random = rng(seed);
        this.dropped = 0;
    }

    /* Something delivered later, or not at all. `reliable' is the room
       socket: delayed, never lost. */
    send (fn, reliable = false)
    {
        if (!reliable && this.random() < this.loss)
        {
            this.dropped++;
            return;
        }

        this.sim.at(this.delay + this.random() * this.jitter, fn);
    }
}

/* One seat: the module, its clocks, and the page's command handling. */
class Peer
{
    constructor (sim, net, spec, M, knobs)
    {
        this.sim = sim;
        this.net = net;
        this.spec = spec;
        this.M = M;
        this.name = spec.name;
        this.frames = spec.startFrame;  /* this context's frame counter */
        this.aligned = false;           /* the module put on it */
        this.tape = '';
        this.lastRender = null;         /* the audio clock's last pair */
        this.others = [];

        this.relayClock = new RelayClock();
        this.audioClock = new AudioClock(spec.rate);
        this.transport = new TransportClock(spec.rate);
        this.dedupe = new Dedupe();
        this.maker = new Maker(spec.name, () => this.transportNow());
        this.late = [];                 /* commands the page saw were late */
        this.listens = new Set();

        for (let c = 0; c < 16; c++)
            if (M._tw_listens(c))
                this.listens.add(c);

        /* host.js's object, over the module directly. The two stamped
           ops go through render.mjs's schedule(), which is the one place
           the op numbers are written down on this side. */
        this.synth = {
            begin: (frame) => M._tw_begin(frame),
            transportAt: (op, at, value = 0) =>
                schedule(M, { op, at, value }),
            knob: (knob, value, at) =>
                schedule(M, { op: 'knob', at, knob, value }),
            noteOn: (note, velocity, frame, channel) =>
                M._tw_note_on(frame, channel, note, velocity),
            noteOff: (note, frame, channel) =>
                M._tw_note_off(frame, channel, note),
            midiOn: (note, velocity, frame, channel) =>
                M._tw_midi_on(frame, channel, note, velocity),
            midiOff: (note, frame, channel) =>
                M._tw_midi_off(frame, channel, note),
        };

        this.gen = null;                /* set by the script, for a reload */
    }

    /* performance.now(): the simulation's clock, offset by something. */
    perfNow ()
    {
        return this.sim.t + this.spec.perfOffset;
    }

    contextTime ()
    {
        return this.frames / this.spec.rate;
    }

    transportNow ()
    {
        return this.transport.now(this.contextTime(), this.perfNow());
    }

    /* The worklet's process(): one block, the tape drained and the page
       told where the transport is. */
    render ()
    {
        const { M } = this;

        /* The worklet's first process(): the module's frame counter put
           on this context's, before anything is rendered under the other
           numbering (worklet.js, and thinkweb.cpp's tw_align). */
        if (!this.aligned)
        {
            M._tw_align(this.frames);
            this.aligned = true;
        }

        M._tw_render(this.spec.block);
        this.frames += this.spec.block;

        for (const e of drain(M))
            this.tape += tapeLine(e);

        this.transport.report({ now: M._tw_now(), origin: M._tw_origin(),
                                running: M._tw_running() !== 0 },
                              this.perfNow());

        /* getOutputTimestamp(): the pair a browser reports, which is
           where the output is and when that was, taken together. The
           output is at the start of the block just rendered -- the block
           before it is what is playing while this one is made -- and this
           simulation has no latency beyond that. */
        this.lastRender = {
            contextTime: (this.frames - this.spec.block) / this.spec.rate,
            performanceTime: this.perfNow(),
        };
    }

    /* Rendering, block after block, for ever. The first block at this
       peer's phase, so two peers' blocks never line up. */
    startRendering ()
    {
        const ms = this.spec.block / this.spec.rate * 1000;
        const tick = () =>
        {
            this.render();
            this.sim.at(ms, tick);
        };

        this.sim.at(this.spec.phase, tick);
    }

    /* A ping to the relay, once a second, and an audio-clock sample beside
       it, as the page does. */
    startPinging (relay, every = 1000, first = 0)
    {
        const ping = () =>
        {
            const t0 = this.perfNow();

            relay.ping((t1) =>
            {
                this.relayClock.sample(t0, t1, this.perfNow());
            });

            if (this.lastRender !== null)
                this.audioClock.sample(this.lastRender.contextTime,
                                       this.lastRender.performanceTime);

            this.sim.at(every, ping);
        };

        this.sim.at(first, ping);
    }

    frameOfOrigin (relayMs)
    {
        return frameOfRelayMs(relayMs, this.relayClock, this.audioClock);
    }

    /* A command in, from the network or from this peer's own hand. */
    receive (cmd)
    {
        if (!this.dedupe.accept(cmd))
            return;

        if (isLate(cmd, this.transportNow()))
            this.late.push(cmd);

        return apply(cmd, {
            synth: this.synth,
            frameOfOrigin: (ms) => this.frameOfOrigin(ms),
            listens: this.listens,
            load: (c) =>
            {
                /* The page's load: the piece from the document, with the
                   seed Play named. */
                this.M.ccall('tw_piece_load', 'number', ['string', 'number'],
                             [this.gen, c.seed]);
            },
        });
    }

    /* A command of this peer's own: applied here, sent to everyone else. */
    async send (cmd, reliable = false)
    {
        await this.receive(cmd);

        for (const other of this.others)
            this.net.send(() => other.receive(cmd), reliable);
    }
}

class Relay
{
    constructor (sim, net)
    {
        this.sim = sim;
        this.net = net;
    }

    now ()
    {
        return this.sim.t + RELAY_OFFSET;
    }

    /* A ping: the request takes one trip, the answer another, and the
       answer carries the relay's clock as it was in between. */
    ping (answer)
    {
        this.net.send(() =>
        {
            const t1 = this.now();

            this.net.send(() => answer(t1), true);
        }, true);
    }
}

/* The script: Play from A, knobs from both sides, a tempo from each, Stop
   from A. Returns what was stamped, so the reference can be given the same
   stream. */
async function play (sim, relay, peers, knob, seed)
{
    const [A, B] = peers;
    const stamped = [];
    const note = (cmd) => stamped.push(cmd);
    let stopAt = null;

    /* Play, three seconds in: enough pings to have a clock. */
    sim.at(3000, async () =>
    {
        const origin = relay.now() + TRANSPORT_LEAD * 1000;
        const cmd = A.maker.start(origin, 'no-document-here', seed);

        await A.send(cmd, true);
        note(cmd);
    });

    /* Knobs, at times of the script's choosing, from either side. Values
       across the knob's range and not on any grid. */
    if (knob !== null)
    {
        const moves = [
            [A, 6000, 0.31], [B, 7500, 0.77], [A, 9000, 0.05],
            [B, 12000, 0.9], [A, 15100, 0.5], [B, 15130, 0.55],
            [A, 22000, 0.2], [B, 26000, 0.66],
        ];

        for (const [who, t, frac] of moves)
            sim.at(t, async () =>
            {
                const value = knob.min + (knob.max - knob.min) * frac;
                const cmd = who.maker.knob(knob.knob, value);

                await who.send(cmd);
                note(cmd);
            });
    }

    sim.at(18000, async () =>
    {
        const cmd = A.maker.tempo(100);

        await A.send(cmd);
        note(cmd);
    });

    sim.at(25000, async () =>
    {
        const cmd = B.maker.tempo(140);

        await B.send(cmd);
        note(cmd);
    });

    /* Stop, from A, so that the transport has run SECONDS. */
    sim.at(3000 + TRANSPORT_LEAD * 1000 + SECONDS * 1000, async () =>
    {
        const cmd = A.maker.stop();

        await A.send(cmd, true);
        note(cmd);
        stopAt = cmd.at;
    });

    /* Until both have stopped, and a while after for anything in flight. */
    let settle = null;

    await sim.run(() =>
    {
        if (stopAt === null)
            return false;

        if (peers.every((p) => !p.transport.running))
        {
            if (settle === null)
                settle = sim.t + 2000;

            return sim.t >= settle;
        }

        return false;
    });

    return { stamped, stopAt };
}

/* One piece over one network: the peers made, the script run, the tapes
   held against each other and against genwav's. */
async function session (createThinkWeb, piece, dsps, network, seed)
{
    const sim = new Sim();
    const net = new Net(sim, NETWORKS[network], seed);
    const relay = new Relay(sim, net);
    const peers = [];

    for (const spec of PEERS)
    {
        const { M, ok, errors } = await loadPiece(createThinkWeb, {
            rate: spec.rate, windowlen: spec.windowlen, block: spec.block,
            gen: piece.text, instruments: dsps,
        });

        if (!ok)
            return { ok: false, errors };

        const peer = new Peer(sim, net, spec, M);

        peer.gen = piece.text;
        peers.push(peer);
    }

    for (const p of peers)
        p.others = peers.filter((q) => q !== p);

    /* The first shown knob, from the module as the page reads it. */
    const { M } = peers[0];
    let knob = null;

    for (let i = 0; i < M._tw_knob_count() && knob === null; i++)
        if (M._tw_knob_shown(i))
            knob = { knob: i, name: M.UTF8ToString(M._tw_knob_name(i)),
                     min: M._tw_knob_min(i), max: M._tw_knob_max(i) };

    peers[0].startRendering();
    peers[1].startRendering();
    peers[0].startPinging(relay, 1000, 0);
    peers[1].startPinging(relay, 1000, 333);

    const { stamped, stopAt } = await play(sim, relay, peers, knob, 4242);

    return { ok: true, peers, knob, stamped, stopAt, net, relay };
}

/* How far each peer's origin frame is from where the origin truly fell
   on its output, in milliseconds. The tapes do not depend on this -- a
   transport time is frames from the origin, wherever the origin is -- but
   the peers being in time with each other by ear does (JAM_M3.md, risk
   2), and it is what clock.js is for. */
function originError (peer, startCmd)
{
    const trueMs = startCmd.origin - RELAY_OFFSET;         /* sim time */
    const trueFrame = (trueMs - peer.spec.phase) / 1000 * peer.spec.rate +
                      peer.spec.startFrame;

    /* Read in the page's numbering. The module keeps its own counter and
       the two are one counter only because the host aligned them; left
       unaligned the module reaches the frame it was armed with however
       far apart they are late, which is a start skew no tape can show
       and this subtraction can. */
    const originFrame = peer.M._tw_origin() +
                        (peer.frames - peer.M._tw_frame());

    return (originFrame - trueFrame) / peer.spec.rate * 1000;
}

if (import.meta.url === pathToFileURL(process.argv[1]).href)
{
    if (!fs.existsSync(path.join(nodeBuild, 'thinksynth.mjs')))
    {
        process.stdout.write(
            `protocoltest: no Node module in ${nodeBuild}. It is the tape ` +
            'everything here is compared against;\n              build it ' +
            'first -- see the top of wasm/CMakeLists.txt.\n');
        process.exit(1);
    }

    const { default: createThinkWeb } =
        await import(pathToFileURL(path.join(build, 'thinkweb.js')).href);

    const dsps = instruments(build);
    const all = pieces(build).filter((p) => p.seeded);
    let failures = 0;

    process.stdout.write(
        `two peers, ${PEERS.map((p) => `${p.rate / 1000}k/${p.windowlen}`)
                           .join(' and ')}, over a network of ` +
        `${NETWORKS.lan.delay} ms with ${NETWORKS.lan.jitter} ms of jitter ` +
        `and ${NETWORKS.lan.loss * 100}% loss; ${SECONDS} s of transport\n\n`);

    for (const piece of all)
    {
        const r = await session(createThinkWeb, piece, dsps, 'lan', 1);

        if (!r.ok)
        {
            failures++;
            process.stdout.write(`FAIL  ${piece.name.padEnd(14)} did not ` +
                                 `load: ${r.errors.join('; ')}\n`);
            continue;
        }

        const [A, B] = r.peers;
        const a = tapeBefore(A.tape, r.stopAt);
        const b = tapeBefore(B.tape, r.stopAt);
        const complaints = [];

        if (a !== b)
            complaints.push(`the two tapes differ: ${firstDifference(a, b)}`);

        const want = reference(piece.name, nodeBuild, {
            commands: r.stamped, stopAt: r.stopAt,
            knobs: r.knob === null ? {} : { [r.knob.knob]: r.knob.name },
        });

        if (a !== want)
            complaints.push(`A differs from genwav: ` +
                            `${firstDifference(want, a)}`);

        for (const p of r.peers)
        {
            if (p.M._tw_late() !== 0)
                complaints.push(`${p.name} applied ${p.M._tw_late()} ` +
                                'command(s) late');

            const err = originError(p, r.stamped[0]);

            if (!(Math.abs(err) <= NETWORKS.lan.jitter + 1))
                complaints.push(`${p.name}'s origin is ${err.toFixed(2)} ms ` +
                                'off');
        }

        /* A piece that composes nothing on its own -- hands.gen is
           played, not played back -- has an empty tape on both sides and
           in genwav, and that is the agreement, not a failure. */
        if (a.split('\n').length < 2 && want.split('\n').length >= 2)
            complaints.push('an empty tape');

        if (complaints.length > 0)
        {
            failures++;
            process.stdout.write(`FAIL  ${piece.name.padEnd(14)} ` +
                                 `${complaints.join('; ')}\n`);
        }
        else
            process.stdout.write(
                `ok    ${piece.name.padEnd(14)} ` +
                `${String(a.split('\n').length - 1).padStart(5)} events   ` +
                `${r.stamped.length} commands, ${r.net.dropped} lost, ` +
                `origins ${r.peers.map((p) => originError(p, r.stamped[0])
                                                  .toFixed(2) + ' ms')
                                    .join(' and ')} off\n`);
    }

    /* The hazard, shown. Over a network slower than the knob lead the
       knobs arrive late, the receiving peer counts them, and this harness
       has to see that it did. The tapes may part; that is the point. */
    {
        const piece = all.find((p) => p.name === 'airports.gen') ?? all[0];
        const r = await session(createThinkWeb, piece, dsps, 'slow', 2);
        const late = r.peers.map((p) => p.M._tw_late());
        const seen = r.peers.map((p) => p.late.length);
        const which = r.peers.flatMap((p) => p.late.map(
            (c) => `${c.from}#${c.seq} ${c.type}` +
                   `${c.op ? ' ' + c.op : ''} at ${c.at.toFixed(3)}`));

        if (late.every((n) => n === 0))
        {
            failures++;
            process.stdout.write(`FAIL  ${piece.name.padEnd(14)} over ` +
                                 `${NETWORKS.slow.delay} ms no command was ` +
                                 'counted late\n');
        }
        else if (seen.every((n) => n === 0))
        {
            failures++;
            process.stdout.write(`FAIL  ${piece.name.padEnd(14)} the ` +
                                 'worklet counted late commands the page ' +
                                 'could not name\n');
        }
        else
            process.stdout.write(
                `ok    ${piece.name.padEnd(14)} over ${NETWORKS.slow.delay} ` +
                `ms: ${late.join(' and ')} late on the two peers -- ` +
                `${which.slice(0, 3).join(', ')}` +
                `${which.length > 3 ? ', ...' : ''}\n`);
    }

    process.stdout.write(
        `\n${failures === 0
             ? 'two peers at different windows and rates compose one tape ' +
               'from stamped commands, and a late command is seen\n'
             : `${failures} failed\n`}`);
    process.exitCode = failures;
}
