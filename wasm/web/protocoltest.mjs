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
 * protocoltest.mjs -- two peers in one process, on a network made of
 * delay, jitter and loss, composing one piece: is it one tape?
 *
 *   node wasm/web/protocoltest.mjs [BUILD_DIR] [NODE_BUILD_DIR]
 *
 * The room's first gate: two schedulers in one process, which is what the
 * design asks for before any of this gets a UI. No browser, no relay, no
 * sockets: a simulation whose wall clock is a number, with a relay whose
 * clock is another number, and two peers each holding the browser module --
 * the same wasm the worklet runs -- stepped at a window and a rate of its
 * own: 256 at 48 kHz for one, 1024 at 44.1 kHz for the other, their blocks
 * out of phase. Between them, the modules the page itself uses: clock.js
 * makes the maps, commands.js makes and applies the commands.
 *
 * A script presses Play on one peer, moves a knob from each side at times
 * of its own, changes the tempo from each side, and stops. Every command is
 * stamped with the transport time it applies at and sent ahead of it, and
 * every peer, the sender included, applies it at that time inside the step.
 * So the two tapes must be one tape -- and both must be the tape genwav.mjs
 * delivers under Node from the same piece and the same command stream,
 * stepped in windows of 1024 from transport zero with no origin and no
 * network at all. A command applied at a window boundary rather than at
 * its stamp would agree on one machine and part on two.
 *
 * Then the second half: the same script with the delay raised past the
 * knob lead. Now a knob reaches the other peer after its time, that peer
 * applies it late and counts it, the tapes part, and the harness has to
 * see both -- the count, and which command. A hazard that is hidden is
 * worse than one that is shown.
 *
 * Ten seconds in, one peer presses Apply on the piece with a number in its
 * last chain moved: an edit, stamped for the next bar, which every peer and
 * genwav apply at that time.
 *
 * Then keys into hands.gen from both peers, quantised on one channel -- so
 * their keys meet on the grid -- and a bar ahead on another.
 *
 * Then a third peer that joins late: ninety seconds into a two-minute run,
 * with knobs moving, the tempo changing and the piece edited on either side
 * of its arrival.
 * It asks the relay for the run -- the start and the stamped commands since
 * -- steps its transport from the room's origin up to the present without a
 * sound, and plays on from there, moving a knob of its own once it has. Its
 * tape, the whole of it and not only the part after the join, must be the
 * room's.
 *
 * Exit status is the number of failures.
 */

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

import { drain, tapeBefore, tapeLine } from '../tape.mjs';
import { AudioClock, RelayClock, TransportClock, frameOfRelayMs }
    from './clock.js';
import { Dedupe, GRID, KNOB_LEAD, Maker, TRANSPORT_LEAD, apply, catchUp,
         isLate, keyAt, nextBar, replayable } from './commands.js';
import { firstDifference, instruments, pieces, reference }
    from './piececheck.mjs';
import { loadPiece, schedule } from './render.mjs';
import { apply as engineApply } from './engine.js';

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

/* The late joiner: a third rate and a block of its own, and a context that
   starts counting when it arrives. */
const JOINER = { name: 'C', rate: 44100, windowlen: 256, block: 128,
                 phase: 3.1, perfOffset: 77777, startFrame: 0 };

/* When it arrives, in transport seconds, and how long the run is. */
const JOIN_AT = 90;
const LATE_SECONDS = 120;

/* How long, from Start, it may take to catch up, in milliseconds. */
const CATCH_UP_WITHIN = 3000;

/* The pieces it joins: the busiest, since the catching up is stepping
   through everything they composed. */
const LATE_PIECES = ['tide.gen', 'warehouse.gen', 'orrery.gen',
                     'airports.gen'];

/* The relay's clock against the simulation's, in milliseconds: nothing a
   peer can see except through a ping. */
const RELAY_OFFSET = 987654.321;

/* Two networks. The first is a LAN with something on it; the second is
   the one that breaks the knob lead. */
const NETWORKS = {
    lan:  { delay: 40, jitter: 20, loss: 0.02 },

    /* The late joiner's: the LAN without the loss. A gesture the mesh drops
       parts the tapes by design -- nothing resends it, and the page counts
       the gap -- and which one it drops is a matter of the seed, so a
       session about something else would pass or fail on which command
       the dice landed on. */
    still: { delay: 40, jitter: 20, loss: 0 },
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
       socket: delayed, never lost, and what a start and an edit go by
       (jam.js, send). */
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
        this.relay = null;

        /* False for a late joiner until it has caught up: what arrives is
           kept, as the page keeps it before Start (jam.js). */
        this.live = true;
        this.early = [];

        for (let c = 0; c < 16; c++)
            if (M._tw_listens(c))
                this.listens.add(c);

        /* host.js's object, over the module directly. The two stamped
           ops go through render.mjs's schedule(), which is the one place
           the op numbers are written down on this side. */
        this.synth = {
            begin: (frame, from = 0, catchUp = false) =>
                M._tw_begin(frame, from, catchUp ? 1 : 0),

            /* host.js posts a batch as one message so that it lands
               between two renders; here every call is already between
               two renders. */
            batch: (fn) => fn(),

            /* engine.js's own, for the commands that name a stage. */
            param: (cmd) => engineApply(M, { ...cmd, type: 'param' }),
            input: (cmd) => engineApply(M, { ...cmd, type: 'input' }),

            /* engine.js's `edit', over the module directly. */
            edit: (at, text, files = {}, tie = 0) =>
            {
                for (const [name, t] of Object.entries(files))
                    M.ccall('tw_edit_file', null, ['string', 'string'],
                            [name, t]);

                M.ccall('tw_edit', null, ['number', 'string', 'number'],
                        [at, text, tie]);
            },
            pick: (at, name, dsp, tie) =>
                M.ccall('tw_pick', null,
                        ['number', 'string', 'string', 'number'],
                        [at, name, dsp, tie]),
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
            noteAt: (at, channel, note, velocity, on, heard, tie) =>
                M._tw_note_at(at, channel, note, velocity, on ? 1 : 0,
                              heard ? 1 : 0, tie),
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
                                speed: M._tw_speed_now(),
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

        if (!this.live)
        {
            if (replayable(cmd))
                this.early.push(cmd);

            return;
        }

        if (isLate(cmd, this.transportNow()))
            this.late.push(cmd);

        return apply(cmd, this.applying());
    }

    /* Where the transport is, as a tape message says it: what keyAt and
       nextBar work from. */
    report ()
    {
        const { M } = this;

        return { now: M._tw_now(), beat: M._tw_beat(), tempo: M._tw_tempo(),
                 meter: M._tw_meter() };
    }

    /* A key pressed or let go of in `mode', as the page's press and release
       stamp one (jam.js). `held' is what the press returned. */
    press (seat, note, velocity, mode)
    {
        const at = keyAt(mode, this.transportNow(), this.report(),
                         this.maker.knobLead);

        return { at, mode, cmd: this.maker.note(seat, note, velocity,
                                                at < 0 ? 'direct' : mode,
                                                at < 0 ? null : at) };
    }

    release (seat, note, held)
    {
        const grid = GRID * 60 / this.M._tw_tempo();
        const at = keyAt(held.mode, this.transportNow(), this.report(),
                         this.maker.knobLead,
                         held.mode === 'quantised' ? held.at + grid : held.at);

        return this.maker.noteoff(seat, note, at < 0 ? 'direct' : held.mode,
                                  at < 0 ? null : at);
    }

    applying ()
    {
        return {
            synth: this.synth,
            frameOfOrigin: (ms) => this.frameOfOrigin(ms),
            listens: this.listens,
            self: this.name,
            load: (c) =>
            {
                /* The page's load: the piece from the document, with the
                   seed Play named. */
                this.M.ccall('tw_piece_load', 'number', ['string', 'number'],
                             [this.gen, c.seed]);
            },
        };
    }

    /* An edit of this peer's own, at the next bar past the transport lead:
       what the page's Apply sends (jam.js, applyEdit). */
    edit (text)
    {
        const { M } = this;
        const report = { now: M._tw_now(), beat: M._tw_beat(),
                         tempo: M._tw_tempo(), meter: M._tw_meter() };

        return this.maker.edit(nextBar(this.transportNow(), report,
                                       this.maker.transportLead), text);
    }

    /* A pick of this peer's own: at the next bar, or at once while
       stopped, as the page sends one (jam.js, pickInstrument). */
    pick (name, dsp)
    {
        return this.maker.pick(this.transport.running
                                   ? nextBar(this.transportNow(),
                                             this.report(),
                                             this.maker.transportLead)
                                   : -1,
                               name, dsp);
    }

    /* A command of this peer's own: applied here, sent to everyone else,
       and a copy to the relay for whoever joins later (jam.js, send). */
    async send (cmd, reliable = false)
    {
        await this.receive(cmd);

        for (const other of this.others)
            this.net.send(() => other.receive(cmd), reliable);

        if (this.relay !== null)
            this.relay.record(cmd);
    }

    /* A late joiner's Start: the run from the relay, merged with what the
       mesh brought meanwhile, stepped through (jam.js, joinRun). */
    joinRun ()
    {
        return new Promise((resolve) =>
        {
            this.relay.catchUp(async (run) =>
            {
                const byKey = new Map();

                for (const c of [...run.log, ...this.early])
                    byKey.set(`${c.from}#${c.seq}`, c);

                this.early = [];

                for (const c of [run.start, ...byKey.values()])
                    this.dedupe.accept(c);

                await catchUp(run.start, [...byKey.values()],
                              this.applying());
                this.live = true;
                resolve(run);
            });
        });
    }
}

class Relay
{
    constructor (sim, net)
    {
        this.sim = sim;
        this.net = net;
        this.run = null;
    }

    /* A peer's copy of a command it sent, as the relay keeps it
       (relay.mjs): a start begins a run, a stop ends it, anything else
       stamped is logged. Over the room socket, so delayed and never
       lost. */
    record (cmd)
    {
        this.net.send(() =>
        {
            if (cmd.type === 'transport' && cmd.op === 'start')
                this.run = { start: cmd, log: [] };
            else if (cmd.type === 'transport' && cmd.op === 'stop')
                this.run = null;
            else if (this.run !== null && replayable(cmd))
                this.run.log.push(cmd);
        }, true);
    }

    /* A late joiner's request, and the run as it stands when it lands. */
    catchUp (answer)
    {
        this.net.send(() =>
        {
            const run = this.run === null
                ? null : { start: this.run.start, log: [...this.run.log] };

            this.net.send(() => answer(run), true);
        }, true);
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

/* The piece with one plain number in its last chain that has one moved --
 * a whole number up by one, anything else by a tenth -- and still loading:
 * what an Apply mid-run sends. null when no chain has such a number.
 */
async function nudged (createThinkWeb, piece, dsps)
{
    const text = piece.text;
    const chains = [...text.matchAll(/^chain\s+\w+\s*\{/gm)]
        .map((m) => m.index).reverse();

    for (const start of chains)
    {
        const end = text.indexOf('\n};', start);
        const body = text.slice(start, end < 0 ? text.length : end);

        for (const m of body.matchAll(/(\b\w+\s*=\s*)(-?\d+(?:\.\d+)?)(\s*;)/g))
        {
            const v = Number(m[2]);

            if (v === 0)
                continue;

            const to = Number.isInteger(v) ? v + 1
                                           : Number((v * 0.9).toFixed(6));
            const at = start + m.index + m[1].length;
            const next = text.slice(0, at) + String(to) +
                         text.slice(at + m[2].length);
            const { ok } = await loadPiece(createThinkWeb, {
                rate: 48000, windowlen: 256, block: 128, gen: next,
                instruments: dsps,
            });

            if (ok)
                return next;
        }
    }

    return null;
}

/* The script: Play from A, knobs from both sides, a tempo from each, Stop
   from A. Returns what was stamped, so the reference can be given the same
   stream. */
async function play (sim, relay, peers, knob, seed, editText = null)
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
                const cmd = who.maker.knob(knob.name, value);

                await who.send(cmd);
                note(cmd);
            });
    }

    /* An Apply from B, ten seconds in: the edit lands at the next bar,
       on both peers, and the knobs on either side of it go on landing on
       the knob they name. And one from A a moment later, putting the piece
       back, which lands on the same bar: which of the two plays is
       decided by their tie and not by which reached a peer last. */
    if (editText !== null)
    {
        sim.at(10000, async () =>
        {
            const cmd = B.edit(editText);

            await B.send(cmd, true);
            note(cmd);
        });

        sim.at(10005, async () =>
        {
            const cmd = A.edit(A.gen);

            await A.send(cmd, true);
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
async function session (createThinkWeb, piece, dsps, network, seed,
                        editText = null)
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

    /* The first shown knob, from the module as the page reads it: the
       piece's KNOB panel, which is where what a knob is now lives
       (src/KnobPanel.cpp). A hidden knob has no row, so the first row is
       the first knob anyone can move. */
    const knob = firstKnob(peers[0].M);

    peers[0].startRendering();
    peers[1].startRendering();
    peers[0].startPinging(relay, 1000, 0);
    peers[1].startPinging(relay, 1000, 333);

    const { stamped, stopAt } = await play(sim, relay, peers, knob, 4242,
                                           editText);

    return { ok: true, peers, knob, stamped, stopAt, net, relay };
}

/* A mute made before an edit and stamped after it, and one made after it,
   for the same time: the first numbers its chain in a piece that may have
   moved, and is dropped; the second is applied. Through engine.js, the
   worklet's and the mirror's door. A complaint, or null. */
async function staleIndex (createThinkWeb, piece, dsps)
{
    const { M, ok } = await loadPiece(createThinkWeb, {
        gen: piece.text, instruments: dsps,
    });

    if (!ok || M._tw_chain_muted(1) < 0)
        return 'wants a piece of two chains or more';

    M._tw_transport(-1, 0, 0);

    for (const m of [
        { type: 'edit', at: 0.5, text: piece.text, files: {}, tie: 1 },
        { type: 'mute', at: 1, chain: 0, on: true, rev: 0 },
        { type: 'mute', at: 1, chain: 1, on: true, rev: 1 },
    ])
        engineApply(M, m);

    while (M._tw_now() < 1.5)
        M._tw_render(128);

    const muted = [0, 1].map((c) => M._tw_chain_muted(c));

    return M._tw_edit_count() === 1 && muted[0] === 0 && muted[1] === 1
        ? null
        : `${M._tw_edit_count()} edit(s) applied; chains 0 and 1 muted ` +
          `${muted.join(' and ')}, wanted 0 and 1`;
}

/* The first shown knob, as the page reads it (see session). */
function firstKnob (M)
{
    if (M._tw_panel_open(1 /* thPanel::KNOB */, 0, 0) === 0)
        return null;

    const [row] = JSON.parse(M.UTF8ToString(M._tw_panel_json())).rows;

    return row === undefined ? null
                             : { knob: Number(row.id), name: row.knob,
                                 min: row.lo, max: row.hi };
}

/* Two peers playing, and a third arriving JOIN_AT seconds in. Knobs move
 * from both sides before and after it arrives, the tempo changes once
 * either side of it, and once it has caught up it moves the knob too --
 * the joiner as a musician rather than a listener.
 */
async function lateSession (createThinkWeb, piece, dsps, seed, seek = 0,
                            editText = null)
{
    const sim = new Sim();
    const net = new Net(sim, NETWORKS.still, seed);
    const relay = new Relay(sim, net);
    const peers = [];

    for (const spec of [...PEERS, JOINER])
    {
        const { M, ok, errors } = await loadPiece(createThinkWeb, {
            rate: spec.rate, windowlen: spec.windowlen, block: spec.block,
            gen: piece.text, instruments: dsps,
        });

        if (!ok)
            return { ok: false, errors };

        const peer = new Peer(sim, net, spec, M);

        peer.gen = piece.text;
        peer.relay = relay;
        peers.push(peer);
    }

    const [A, B, C] = peers;

    A.others = [B];
    B.others = [A];

    const knob = firstKnob(A.M);
    const stamped = [];
    const note = (cmd) => stamped.push(cmd);
    const origin = 3000 + TRANSPORT_LEAD * 1000;
    const joinMs = origin + JOIN_AT * 1000;
    let stopAt = null;
    let caughtUp = null;          /* sim ms from Start to caught up */

    A.startRendering();
    B.startRendering();
    A.startPinging(relay, 1000, 0);
    B.startPinging(relay, 1000, 333);

    sim.at(3000, async () =>
    {
        const cmd = A.maker.start(relay.now() + TRANSPORT_LEAD * 1000,
                                  'no-document-here', seed, seek);

        await A.send(cmd, true);
        note(cmd);
    });

    if (knob !== null)
    {
        const moves = [];

        /* Every seven seconds or so, alternating, to the end. */
        for (let t = 6000, k = 0; t < origin + LATE_SECONDS * 1000 - 2000;
             t += 6700 + (k % 3) * 900, k++)
            moves.push([k % 2 === 0 ? A : B, t, ((k * 0.37) % 1)]);

        /* And the joiner's own, once it is playing. */
        moves.push([C, joinMs + 15000, 0.12], [C, joinMs + 22000, 0.81]);

        for (const [who, t, frac] of moves)
            sim.at(t, async () =>
            {
                if (!who.live)
                    return;

                const value = knob.min + (knob.max - knob.min) * frac;
                const cmd = who.maker.knob(knob.name, value);

                await who.send(cmd);
                note(cmd);
            });
    }

    /* An edit before the joiner arrives, which it has to step through,
       and the text put back after it has, which it has to apply live. */
    if (editText !== null)
        for (const [who, t, text] of [[B, origin + 50000, editText],
                                      [A, joinMs + 18000, piece.text]])
            sim.at(t, async () =>
            {
                if (!who.live)
                    return;

                const cmd = who.edit(text);

                await who.send(cmd, true);
                note(cmd);
            });

    for (const [who, t, bpm] of [[A, origin + 40000, 100],
                                 [B, joinMs + 10000, 140]])
        sim.at(t, async () =>
        {
            const cmd = who.maker.tempo(bpm);

            await who.send(cmd);
            note(cmd);
        });

    /* The arrival: a context that starts counting now, pings, and the
       mesh to the other two. */
    sim.at(joinMs, () =>
    {
        C.live = false;
        A.others.push(C);
        B.others.push(C);
        C.others = [A, B];
        C.startRendering();
        C.startPinging(relay, 1000, 0);
    });

    /* Start, once its clocks have enough samples (jam.js). */
    sim.at(joinMs + 4500, () =>
    {
        const pressed = sim.t;

        /* Not awaited: the answer is an event of its own on this clock,
           and the clock does not move while an event is awaited. */
        C.joinRun().then(() =>
        {
            const watch = () =>
            {
                if (C.M._tw_catching() === 0)
                    caughtUp = sim.t - pressed;
                else
                    sim.at(5, watch);
            };

            sim.at(5, watch);
        });
    });

    sim.at(origin + LATE_SECONDS * 1000, async () =>
    {
        const cmd = A.maker.stop();

        await A.send(cmd, true);
        note(cmd);
        stopAt = cmd.at;
    });

    let settle = null;

    await sim.run(() =>
    {
        if (stopAt === null || !peers.every((p) => !p.transport.running))
            return false;

        settle ??= sim.t + 2000;

        return sim.t >= settle;
    });

    return { ok: true, peers, knob, stamped, stopAt, caughtUp };
}

/* What the edits of a session were, for its line. */
function editNote (r)
{
    const edits = r.stamped.filter((c) => c.type === 'edit');

    if (edits.length === 0)
        return '';

    const bars = new Set(edits.map((c) => c.at));

    return ` with ${edits.length} edits` +
           (bars.size < edits.length ? ' on one bar' : '');
}

/* Two peers playing into hands.gen: keys into a piece, which is the case
 * direct mode cannot serve. Both quantised on one channel, so their keys
 * meet on the same grid lines and the tie decides their order; one of them
 * a bar ahead on another. Returns what session() does.
 */
async function handsSession (createThinkWeb, piece, dsps)
{
    const sim = new Sim();
    const net = new Net(sim, NETWORKS.still, 5);
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

    const [A, B] = peers;

    A.others = [B];
    B.others = [A];

    /* hands.gen's chains in order: an arpeggiator, a quantizer, and
       another arpeggiator. The keys that meet go to the quantizer, which
       passes them on in the order they reach it; an arpeggiator sorts what
       it holds, and would hide the order. */
    const [arp, corrected] = [...A.listens].sort((x, y) => x - y);
    const stamped = [];
    const ahead = [];               /* [pressed at, stamped for] */
    const origin = 3000 + TRANSPORT_LEAD * 1000;
    let stopAt = null;

    A.startRendering();
    B.startRendering();
    A.startPinging(relay, 1000, 0);
    B.startPinging(relay, 1000, 333);

    sim.at(3000, async () =>
    {
        const cmd = A.maker.start(relay.now() + TRANSPORT_LEAD * 1000,
                                  'no-document-here', 4242);

        await A.send(cmd, true);
        stamped.push(cmd);
    });

    /* A key down at `t', up at `t + length', from `who' in `mode'. */
    const key = (who, t, seat, note, length, mode) =>
    {
        let held = null;

        sim.at(t, async () =>
        {
            const pressedAt = who.transportNow();

            held = who.press(seat, note, 90, mode);
            await who.send(held.cmd);
            stamped.push(held.cmd);

            if (mode === 'ahead')
                ahead.push([pressedAt, held.at]);
        });

        sim.at(t + length, async () =>
        {
            const cmd = who.release(seat, note, held);

            await who.send(cmd);
            stamped.push(cmd);
        });
    };

    for (let k = 0; k < 12; k++)
    {
        const t = origin + 2500 + k * 1300;

        for (const n of [53, 56, 60])
            key(A, t, corrected, n + (k % 3), 600 + k * 37, 'quantised');

        for (const n of [58, 61])
            key(B, t + 3, corrected, n, 450, 'quantised');

        key(B, t + 200, arp, 65 + (k % 4), 900, 'ahead');
    }

    /* Partway through, an edit moves the quantizer's sink to channel 9,
       so it no longer takes keys on the channel they are played on, and a
       later one puts it back: the keys between go onto the channel and not
       into the piece, on every peer, however long each peer's page went on
       believing the piece took them -- whether a key goes in is the
       worklet's to say when it applies. */
    const deaf = piece.text.replace(
        /(chain corrected \{[\s\S]*?sink \{ channel = )2;/, '$19;');

    if (deaf === piece.text)
        throw new Error('hands.gen: no sink on corrected to move');

    for (const [who, t, text] of [[B, origin + 2500 + 5 * 1300 + 700, deaf],
                                  [A, origin + 2500 + 8 * 1300 + 700,
                                   piece.text]])
        sim.at(t, async () =>
        {
            const cmd = who.edit(text);

            await who.send(cmd);
            stamped.push(cmd);
        });

    sim.at(origin + SECONDS * 1000, async () =>
    {
        const cmd = A.maker.stop();

        await A.send(cmd, true);
        stamped.push(cmd);
        stopAt = cmd.at;
    });

    let settle = null;

    await sim.run(() =>
    {
        if (stopAt === null || !peers.every((p) => !p.transport.running))
            return false;

        settle ??= sim.t + 2000;

        return sim.t >= settle;
    });

    return { ok: true, peers, stamped, stopAt, ahead };
}

/* The graph instrument `name' plays in a peer's module, as the room page's
   seat list reads it. */
function graphOf (M, name)
{
    for (let i = 0; i < M._tw_instrument_count(); i++)
        if (M.UTF8ToString(M._tw_instrument_name(i)) === name)
            return M.UTF8ToString(M._tw_instrument_dsp(i));

    return null;
}

/* The three seats freeSession picks for. */
const PICKED = ['one', 'two', 'three'];

/* Two peers in a free room: free.gen, an instrument per seat and nothing
 * composed. While stopped, A picks a graph for seat two, which every peer
 * puts on at once; A's document has it too, which is what the Play loads.
 * Then both play seat one, one quantized and one a bar ahead, and partway
 * through A picks for seat one and B for seat three a few milliseconds
 * apart: one bar for both, and neither pick carries the other's text.
 * Every render while playing, on both peers, is noted with the graph each
 * seat plays after it. Returns what handsSession does, the bar the two
 * picks landed on, and what each peer played seat two on before the Play.
 */
async function freeSession (createThinkWeb, piece, dsps)
{
    const sim = new Sim();
    const net = new Net(sim, NETWORKS.still, 7);
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
        const render = peer.render.bind(peer);

        peer.gen = piece.text;
        peer.graphs = [];
        peer.render = () =>
        {
            const from = M._tw_now();

            render();

            if (M._tw_running())
                peer.graphs.push([from, M._tw_now(),
                                  PICKED.map((n) => graphOf(M, n))]);
        };
        peers.push(peer);
    }

    const [A, B] = peers;
    const relay = new Relay(sim, net);
    const seat = Math.min(...A.listens);
    const stamped = [];
    const origin = 3000 + TRANSPORT_LEAD * 1000;
    const before = [];
    let stopAt = null;
    let picked = null;

    A.others = [B];
    B.others = [A];
    A.startRendering();
    B.startRendering();
    A.startPinging(relay, 1000, 0);
    B.startPinging(relay, 1000, 333);

    const send = (who, t, make) => sim.at(t, async () =>
    {
        const cmd = make();

        await who.send(cmd, cmd.type === 'transport' || cmd.type === 'pick');
        stamped.push(cmd);
    });

    /* Stopped: the document as the picker splices it, for the Play. */
    send(A, 1000, () =>
    {
        const text = A.M.ccall('tw_gen_set_instrument', 'string',
                               ['string', 'string', 'string'],
                               [piece.text, 'two', 'pluck.dsp']);

        A.gen = B.gen = text;
        return A.pick('two', 'pluck.dsp');
    });

    sim.at(2900, () =>
    {
        for (const p of peers)
            before.push(graphOf(p.M, 'two'));
    });

    send(A, 3000, () => A.maker.start(relay.now() + TRANSPORT_LEAD * 1000,
                                      'no-document-here', 4242));

    for (let k = 0; k < 14; k++)
    {
        const t = origin + 1500 + k * 700;
        const [who, mode] = k % 2 === 0 ? [A, 'quantised'] : [B, 'ahead'];
        const note = 60 + (k % 5);
        let held = null;

        send(who, t, () =>
        {
            held = who.press(seat, note, 90, mode);
            return held.cmd;
        });
        send(who, t + 400, () => who.release(seat, note, held));
    }

    send(A, origin + 5200, () =>
    {
        const cmd = A.pick('one', 'dxbell.dsp');

        picked = cmd.at;
        return cmd;
    });
    send(B, origin + 5205, () => B.pick('three', 'strings.dsp'));

    send(A, origin + 12000, () =>
    {
        const cmd = A.maker.stop();

        stopAt = cmd.at;
        return cmd;
    });

    let settle = null;

    await sim.run(() =>
    {
        if (stopAt === null || !peers.every((p) => !p.transport.running))
            return false;

        settle ??= sim.t + 2000;

        return sim.t >= settle;
    });

    return { ok: true, peers, stamped, stopAt, picked, before };
}

/* A command that names a stage, applied after an edit that moved it.
 *
 * loosen.gen's chains are grid, breathed and corrected, each with a
 * humanize stage `h' in the last two. An edit puts a chain above them all,
 * so corrected is chain 3 afterwards and chain 2 is breathed. Then a param
 * for corrected's `h' arrives, stamped for after the edit and numbered as
 * the piece was before it -- what a page whose composer view had not
 * caught up would send. By its numbers it would set breathed's; by its
 * names, corrected's. With `input', a press in the middle of corrected's
 * euclid ring and its release instead, which writes one more fill.
 */
async function movedSession (createThinkWeb, piece, dsps, named = true,
                             input = false)
{
    const sim = new Sim();
    const net = new Net(sim, NETWORKS.still, 7);
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

    const [A, B] = peers;

    A.others = [B];
    B.others = [A];

    const start = piece.text.indexOf('chain grid {');
    const end = piece.text.indexOf('\n};\n', start) + 4;
    const extra = piece.text.slice(start, end)
        .replace('chain grid {', 'chain extra {') + '\n';
    const moved = piece.text.slice(0, start) + extra +
                  piece.text.slice(start);
    let editAt = null;
    let stopAt = null;

    A.startRendering();
    B.startRendering();
    A.startPinging(relay, 1000, 0);
    B.startPinging(relay, 1000, 333);

    sim.at(3000, async () =>
        A.send(A.maker.start(relay.now() + TRANSPORT_LEAD * 1000,
                             'no-document-here', 4242), true));

    sim.at(6000, async () =>
    {
        const cmd = A.edit(moved);

        editAt = cmd.at;
        await A.send(cmd);
    });

    /* Well after the edit's bar, stamped for later still. Named, it is
       made as before the edit too (rev 0): its names, not that, decide
       where it lands. */
    const names = !named ? {}
        : { chainName: 'corrected', stageName: input ? 'src' : 'h', rev: 0 };

    sim.at(10000, async () =>
        B.send(input ? B.maker.input(2, 0, 0, 50, 50, 100, 100, 1, names)
                     : B.maker.param(2, 1, 'vel', '20', names)));

    if (input)
        sim.at(10100, async () =>
            B.send(B.maker.input(2, 0, 2, 50, 50, 100, 100, 1, names)));

    sim.at(14000, async () =>
    {
        const cmd = A.maker.stop();

        stopAt = cmd.at;
        await A.send(cmd, true);
    });

    let settle = null;

    await sim.run(() =>
    {
        if (stopAt === null || !peers.every((p) => !p.transport.running))
            return false;

        settle ??= sim.t + 1000;

        return sim.t >= settle;
    });

    return { ok: true, peers, editAt, stopAt };
}

/* A stage's param in one chain of a piece's text. */
function settingOf (text, chain, stage, param)
{
    const start = text.indexOf(`chain ${chain} {`);
    const body = text.slice(start, text.indexOf('\n};', start));
    const at = body.slice(body.indexOf(`stage ${stage} `));

    const value = new RegExp(`\\b${param}\\s*=\\s*([\\d.]+)`).exec(at);

    return value?.[1] ?? null;
}

/* What movedSession's command did, as complaints: `param' of `stage' is
   `set' in corrected and still `was' in breathed. */
function movedComplaints (r, { stage, param, set, was })
{
    const complaints = [];

    if (!r.ok)
        return [`did not load: ${r.errors.join('; ')}`];

    const [A, B] = r.peers;
    const texts = r.peers.map((p) => p.M.UTF8ToString(p.M._tw_piece_text()));

    if (texts[0] !== texts[1])
        complaints.push('the two peers hold different texts');

    const corrected = settingOf(texts[0], 'corrected', stage, param);
    const breathed = settingOf(texts[0], 'breathed', stage, param);

    if (corrected !== set)
        complaints.push(`corrected's ${stage} has ${param} ${corrected}, ` +
                        `not ${set}`);

    if (breathed !== was)
        complaints.push(`breathed's ${stage} was changed to ${breathed}`);

    if (tapeBefore(A.tape, r.stopAt) !== tapeBefore(B.tape, r.stopAt))
        complaints.push('the two tapes differ');

    for (const p of r.peers)
        if (p.M._tw_edit_count() !== 1)
            complaints.push(`${p.name} applied ${p.M._tw_edit_count()} ` +
                            'edits');

    return complaints;
}

/* Every edit that was sent, applied whole on every peer. */
function editComplaints (r)
{
    const sent = r.stamped.filter((c) => c.type === 'edit').length;
    const out = [];

    for (const p of r.peers)
    {
        const got = p.M._tw_edit_count();
        const said = p.M._tw_edit_error_count();

        if (got !== sent || said !== 0)
            out.push(`${p.name} applied ${got} of ${sent} edits` +
                     (said !== 0
                          ? `: ${p.M.UTF8ToString(p.M._tw_edit_error(0))}`
                          : ''));
    }

    return out;
}

/* How far each peer's origin frame is from where the origin truly fell on
   its output, in milliseconds. The tapes do not depend on this -- a
   transport time is frames from the origin, wherever the origin is -- but
   the peers being in time with each other by ear does, and it is what
   clock.js is for. */
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
    /* PROTOCOLTEST_ONLY=name.gen runs one piece, and PROTOCOLTEST_DUMP=dir
       writes each peer's tape there: for reading a failure. */
    const all = pieces(build).filter((p) => p.seeded &&
        (!process.env.PROTOCOLTEST_ONLY ||
         p.name === process.env.PROTOCOLTEST_ONLY));
    const dump = (piece, peers) =>
    {
        if (process.env.PROTOCOLTEST_DUMP)
            for (const p of peers)
                fs.writeFileSync(path.join(process.env.PROTOCOLTEST_DUMP,
                                           `${piece.name}.${p.name}`), p.tape);
    };
    let failures = 0;

    process.stdout.write(
        `two peers, ${PEERS.map((p) => `${p.rate / 1000}k/${p.windowlen}`)
                           .join(' and ')}, over a network of ` +
        `${NETWORKS.lan.delay} ms with ${NETWORKS.lan.jitter} ms of jitter ` +
        `and ${NETWORKS.lan.loss * 100}% loss; ${SECONDS} s of transport\n\n`);

    for (const piece of all)
    {
        const r = await session(createThinkWeb, piece, dsps, 'lan', 1,
                                await nudged(createThinkWeb, piece, dsps));

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
        });

        complaints.push(...editComplaints(r));

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
                `${r.stamped.length} commands` +
                `${editNote(r)}, ` +
                `${r.net.dropped} lost, ` +
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

    /* Two peers' first Plays are two commands. */
    {
        const dedupe = new Dedupe();
        const starts = [new Maker('a', () => 0).start(0, 'h', 1),
                        new Maker('b', () => 0).start(0, 'h', 1)];

        if (!starts.every((c) => dedupe.accept(c)))
        {
            failures++;
            process.stdout.write('FAIL  two peers\' Plays: the second was ' +
                                 `taken for the first (from ` +
                                 `${starts.map((c) => c.from).join(', ')})\n`);
        }
        else
            process.stdout.write('ok    two peers\' Plays are both played\n');
    }

    /* A key's reliable copy held up behind a knob drag is still a copy. */
    {
        const dedupe = new Dedupe();
        const maker = new Maker('a', () => 0);
        const key = maker.note(0, 60, 90);
        const drag = Array.from({ length: 1000 },
                                () => maker.knob('cutoff', 0.5));

        if ([key, ...drag].every((c) => dedupe.accept(c)) &&
            !dedupe.accept(key))
            process.stdout.write('ok    a key\'s second copy a thousand ' +
                                 'knobs late is dropped\n');
        else
        {
            failures++;
            process.stdout.write('FAIL  a key\'s second copy a thousand ' +
                                 'knobs late was applied again\n');
        }
    }

    {
        const piece = all.find((p) => p.name === 'airports.gen') ?? all[0];
        const why = await staleIndex(createThinkWeb, piece, dsps);

        if (why !== null)
        {
            failures++;
            process.stdout.write(`FAIL  ${piece.name.padEnd(14)} a mute ` +
                                 `stamped across an edit: ${why}\n`);
        }
        else
            process.stdout.write(`ok    ${piece.name.padEnd(14)} a mute ` +
                                 'made before an edit and stamped after it ' +
                                 'is dropped\n');
    }

    /* Keys into a piece. */
    {
        const piece = pieces(build).find((p) => p.name === 'hands.gen');
        const r = await handsSession(createThinkWeb, piece, dsps);

        process.stdout.write('\nkeys into hands.gen, quantised from both ' +
                             'peers on one channel and a bar ahead on ' +
                             'another\n\n');

        if (!r.ok)
        {
            failures++;
            process.stdout.write(`FAIL  hands.gen did not load: ` +
                                 `${r.errors.join('; ')}\n`);
        }
        else
        {
            const [A, B] = r.peers;
            const a = tapeBefore(A.tape, r.stopAt);
            const b = tapeBefore(B.tape, r.stopAt);
            const complaints = [];

            if (a !== b)
                complaints.push(`the two tapes differ: ` +
                                `${firstDifference(a, b)}`);

            const want = reference('hands.gen', nodeBuild, {
                commands: r.stamped, stopAt: r.stopAt,
            });

            if (a !== want)
                complaints.push(`A differs from genwav: ` +
                                `${firstDifference(want, a)}`);

            for (const p of r.peers)
                if (p.M._tw_late() !== 0)
                    complaints.push(`${p.name} applied ${p.M._tw_late()} ` +
                                    'command(s) late');

            complaints.push(...editComplaints(r));

            /* Keys from the two peers that met on one grid line, which is
               what the tie is there for. */
            const lines = new Map();

            for (const c of r.stamped)
                if (c.type === 'note' && c.mode === 'quantised')
                {
                    const from = lines.get(c.at) ?? new Set();

                    from.add(c.from);
                    lines.set(c.at, from);
                }

            const met = [...lines.values()].filter((s) => s.size > 1).length;

            if (met === 0)
                complaints.push('no two peers\' keys met on a grid line');

            /* A bar ahead is a bar: two seconds at 120 and four beats. */
            const bar = 4 * 60 / 120;

            for (const [pressed, at] of r.ahead)
                if (Math.abs(at - pressed - bar) > 1e-6)
                    complaints.push(`a key a bar ahead was stamped ` +
                                    `${(at - pressed).toFixed(3)} s on`);

            if (a.split('\n').length < 20)
                complaints.push('the piece hardly played');

            if (complaints.length > 0)
            {
                failures++;
                process.stdout.write(`FAIL  hands.gen      ` +
                                     `${complaints.join('; ')}\n`);
            }
            else
                process.stdout.write(
                    `ok    hands.gen      ${String(a.split('\n').length - 1)
                                             .padStart(5)} events from ` +
                    `${r.stamped.filter((c) => c.type === 'note').length} ` +
                    `keys; ${met} grid lines where the peers' keys met\n`);
        }
    }

    /* Instruments picked for seats, stopped and while the room plays. */
    {
        const piece = pieces(build).find((p) => p.name === 'free.gen');
        const r = await freeSession(createThinkWeb, piece, dsps);

        process.stdout.write('\ninstruments picked for seats of free.gen, ' +
                             'stopped and while two peers play it\n\n');

        if (!r.ok)
        {
            failures++;
            process.stdout.write(`FAIL  free.gen did not load: ` +
                                 `${r.errors.join('; ')}\n`);
        }
        else
        {
            const [A, B] = r.peers;
            const a = tapeBefore(A.tape, r.stopAt);
            const b = tapeBefore(B.tape, r.stopAt);
            const complaints = [];
            /* The Play's load starts the count again. */
            const picks = r.stamped.filter((c) => c.type === 'pick' &&
                                                  c.at >= 0);
            const bars = new Set(picks.map((c) => c.at));

            for (const p of r.peers)
                if (p.M._tw_edit_count() !== picks.length ||
                    p.M._tw_edit_error_count() !== 0)
                    complaints.push(`${p.name} applied ` +
                                    `${p.M._tw_edit_count()} of ` +
                                    `${picks.length} picks`);

            if (bars.size !== 1)
                complaints.push(`the two picks while playing landed on ` +
                                `${bars.size} bars`);

            if (r.before.some((g) => g !== 'pluck.dsp'))
                complaints.push(`seat two before the Play: ` +
                                `${r.before.join(', ')}`);

            if (a !== b)
                complaints.push(`the two tapes differ: ` +
                                `${firstDifference(a, b)}`);

            const want = reference('free.gen', nodeBuild, {
                commands: r.stamped.filter((c) => c.at >= 0),
                stopAt: r.stopAt,
            });

            if (a !== want)
                complaints.push(`A differs from genwav: ` +
                                `${firstDifference(want, a)}`);

            if (a.split('\n').length < 10)
                complaints.push('the seat hardly played');

            /* Up to the bar the old graphs and from the window after it
               the new ones, on both peers: the window is as late as a
               command stamped inside one may land. Seat two stays on the
               graph picked while stopped, through the Play's load. */
            const was = ['rhodes.dsp', 'pluck.dsp', 'ebass.dsp'];
            const now = ['dxbell.dsp', 'pluck.dsp', 'strings.dsp'];

            for (const p of r.peers)
            {
                const window = p.spec.windowlen / p.spec.rate;
                const wrong = p.graphs.find(([from, to, dsps]) =>
                    (to < r.picked && dsps.join() !== was.join()) ||
                    (from >= r.picked + window && dsps.join() !== now.join()));

                if (wrong !== undefined)
                    complaints.push(`${p.name} played ` +
                                    `${wrong[2].join(', ')} at ` +
                                    `${wrong[0].toFixed(3)} s, the picks ` +
                                    `landing at ${r.picked.toFixed(3)} s`);
            }

            if (complaints.length > 0)
            {
                failures++;
                process.stdout.write(`FAIL  free.gen       ` +
                                     `${complaints.join('; ')}\n`);
            }
            else
                process.stdout.write(
                    `ok    free.gen       ${String(a.split('\n').length - 1)
                                             .padStart(5)} events; seat two ` +
                    'on pluck.dsp before the Play, seats one and three ' +
                    `picked from two peers on one bar, ` +
                    `${r.picked.toFixed(3)} s, and both picks kept\n`);
        }
    }

    /* A command for a stage an edit moved: by name it reaches it, and the
       same command without its names reaches the neighbor -- the second
       run is what says the first one tested anything. */
    process.stdout.write('\na param and a gesture numbered for the piece ' +
                         'before an edit that moved their stage\n\n');

    for (const [what, input, want] of [
        ['param', false, { stage: 'h', param: 'vel', set: '20', was: '14' }],
        ['gesture', true,
         { stage: 'src', param: 'fills', set: '8', was: '7' }],
    ])
    {
        const piece = pieces(build).find((p) => p.name === 'loosen.gen');
        const r = await movedSession(createThinkWeb, piece, dsps, true,
                                     input);
        const complaints = movedComplaints(r, want);
        const unnamed = movedComplaints(
            await movedSession(createThinkWeb, piece, dsps, false, input),
            want);

        if (!unnamed.some((c) => /breathed's .* was changed/.test(c)))
            complaints.push(`without its names the ${what} did not go ` +
                            'astray, so this proves nothing');

        if (complaints.length > 0)
        {
            failures++;
            process.stdout.write(`FAIL  loosen.gen     ${what}: ` +
                                 `${complaints.join('; ')}\n`);
        }
        else
            process.stdout.write(
                `ok    loosen.gen     the edit at ${r.editAt.toFixed(3)} ` +
                `moved corrected from chain 2 to 3; the ${what} numbered 2 ` +
                `reached it by name, and without its names reached ` +
                `breathed\n`);
    }

    /* The late joiner. */
    process.stdout.write(
        `\na third peer, ${JOINER.rate / 1000}k/${JOINER.windowlen}, ` +
        `joining ${JOIN_AT} s into ${LATE_SECONDS} s\n\n`);

    for (const piece of all.filter((p) => LATE_PIECES.includes(p.name)))
    {
        const r = await lateSession(createThinkWeb, piece, dsps, 3, 0,
                                    await nudged(createThinkWeb, piece, dsps));

        if (!r.ok)
        {
            failures++;
            process.stdout.write(`FAIL  ${piece.name.padEnd(14)} did not ` +
                                 `load: ${r.errors.join('; ')}\n`);
            continue;
        }

        const [A, B, C] = r.peers;

        dump(piece, r.peers);
        const [a, b, c] = [A, B, C].map((p) => tapeBefore(p.tape, r.stopAt));
        const complaints = [];

        if (a !== b)
            complaints.push(`A and B differ: ${firstDifference(a, b)}`);

        if (c !== a)
            complaints.push(`the joiner differs: ${firstDifference(a, c)}`);

        const want = reference(piece.name, nodeBuild, {
            commands: r.stamped, stopAt: r.stopAt,
        });

        complaints.push(...editComplaints(r));

        if (a !== want)
            complaints.push(`A differs from genwav: ` +
                            `${firstDifference(want, a)}`);

        for (const p of r.peers)
            if (p.M._tw_late() !== 0)
                complaints.push(`${p.name} applied ${p.M._tw_late()} ` +
                                'command(s) late');

        /* Stepping a silent synth through a minute and a half is tens of
           milliseconds of work, a slice of each window at a time; seconds
           of silence would be a joiner stuck behind the room. */
        if (r.caughtUp === null)
            complaints.push('the joiner never caught up');
        else if (r.caughtUp > CATCH_UP_WITHIN)
            complaints.push(`the joiner took ${r.caughtUp.toFixed(0)} ms ` +
                            'to catch up');

        const after = c.split('\n').filter(
            (l) => parseFloat(l.split(' ')[1]) >= JOIN_AT).length;

        if (complaints.length > 0)
        {
            failures++;
            process.stdout.write(`FAIL  ${piece.name.padEnd(14)} ` +
                                 `${complaints.join('; ')}\n`);
        }
        else
            process.stdout.write(
                `ok    ${piece.name.padEnd(14)} ` +
                `${String(c.split('\n').length - 1).padStart(5)} events, ` +
                `${after} after the join; caught up ` +
                `${r.caughtUp.toFixed(0)} ms after Start, ` +
                `${r.stamped.length} commands\n`);
    }

    /* A Play from a time: the joiner's transport zero is where the seek
       put the room's, and the sender's id is not a time. */
    {
        const piece = all.find((p) => p.name === 'orrery.gen');
        const r = await lateSession(createThinkWeb, piece, dsps, 3, 30);
        const [a, b, c] = r.ok ? r.peers.map((p) => tapeBefore(p.tape,
                                                               r.stopAt))
                               : [];

        if (!r.ok || a !== b || c !== a)
        {
            failures++;
            process.stdout.write(
                `FAIL  a seek 30 s in: ${!r.ok ? r.errors.join('; ')
                                     : a !== b ? firstDifference(a, b)
                                     : firstDifference(a, c)}\n`);
        }
        else
            process.stdout.write('ok    a seek 30 s in, joined and caught ' +
                                 'up with\n');
    }

    process.stdout.write(
        `\n${failures === 0
             ? 'two peers at different windows and rates compose one tape ' +
               'from stamped commands, a late command is seen, and a peer ' +
               'joining late composes the same tape\n'
             : `${failures} failed\n`}`);
    process.exitCode = failures;
}
