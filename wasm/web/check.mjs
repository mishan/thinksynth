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
 * check.mjs -- the browser build, played from Node.
 *
 *   node wasm/web/check.mjs [BUILD_DIR]
 *
 * Every .dsp the page offers is loaded into the module the page runs, and a
 * note is played through it the way the worklet plays one: stamped, a
 * 128-frame block at a time, at 48 kHz in windows of 256. Each must load,
 * make sound, make nothing that is not a number, and make the same sound
 * twice. Then one stamp is checked for where it lands -- in the window its
 * frame falls in, not before and not a window late.
 *
 * What this cannot see is the browser: the worklet, its messages, and the
 * quanta a real audio thread asks for. browsertest.mjs holds that against
 * this, to the bit.
 *
 * Exit status is the number of failures.
 */

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

import { renderDirect } from './render.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const build = path.resolve(process.argv[2] ??
                           path.join(here, '..', '..', 'build-web'));

const { default: createThinkWeb } =
    await import(pathToFileURL(path.join(build, 'thinkweb.js')).href);

const RATE = 48000;
const FRAMES = RATE;           /* one second */
const NOTE = { note: 60, velocity: 100 };

let failures = 0;

const fail = (what) =>
{
    process.stdout.write(`FAIL  ${what}\n`);
    failures++;
};

const dspDir = path.join(build, 'dsp');
const names = JSON.parse(fs.readFileSync(path.join(dspDir, 'index.json'),
                                         'utf8'));

/* The kit, as bytes, handed to every render below. A graph with an
   osc::sample node in it -- dsp/linn.dsp, dsp/orchhit.dsp -- has nothing
   to play without them, and a sampler with no file is silence, which is
   exactly what this gate calls a failure. */
const kit = Object.fromEntries(
    names.filter((n) => n.startsWith('samples/'))
         .map((n) => [n, new Uint8Array(fs.readFileSync(
                            path.join(dspDir, n)))]));

for (const name of names)
{
    /* An effect graph has no note to play and is not an instrument; the
       index carries it so the worklet can be handed it. fxcheck is its
       gate. */
    if (name.startsWith('fx/'))
    {
        process.stdout.write(`skip  ${name}: an effect graph\n`);
        continue;
    }

    /* And the kit is not a graph at all -- it is the wavs osc::sample
       plays, carried in the same index because the worklet has to be
       handed them and cannot fetch. statecheck is their gate. */
    if (name.startsWith('samples/'))
    {
        process.stdout.write(`skip  ${name}: a sample, not a graph\n`);
        continue;
    }

    const text = fs.readFileSync(path.join(dspDir, name), 'utf8');
    const events = [
        { on: true, frame: 0, ...NOTE },
        { on: false, frame: RATE / 2, note: NOTE.note },
    ];

    const a = await renderDirect(createThinkWeb,
                                 { rate: RATE, text, events, samples: kit,
                                   frames: FRAMES });

    if (!a.ok)
    {
        fail(`${name}: did not load\n      ${a.log.join('\n      ')}`);
        continue;
    }

    let peak = 0, bad = 0;

    for (const v of a.out)
    {
        if (!Number.isFinite(v))
            bad++;
        else
            peak = Math.max(peak, Math.abs(v));
    }

    const b = await renderDirect(createThinkWeb,
                                 { rate: RATE, text, events, samples: kit,
                                   frames: FRAMES });
    const same = Buffer.from(a.out.buffer).equals(Buffer.from(b.out.buffer));

    if (bad > 0)
        fail(`${name}: ${bad} samples that are not numbers`);
    else if (peak === 0)
        fail(`${name}: silent`);
    else if (!same)
        fail(`${name}: two renders differ`);
    else
        process.stdout.write(`ok    ${name.padEnd(14)} peak ${peak.toFixed(3)}\n`);
}

/* A note stamped at frame 1000 is applied in the window 768-1023, and a note
   sounds from the window after the one it is added in -- the engine's own
   onset, the desktop's too. So: nothing before 1024, something before 1280.
   A stamp a window late or early moves that by a window either way. */
{
    const text = fs.readFileSync(path.join(dspDir, 'ts1.dsp'), 'utf8');
    const r = await renderDirect(createThinkWeb, {
        rate: RATE, text, frames: 4096,
        events: [ { on: true, frame: 1000, ...NOTE } ],
    });

    let first = -1;

    for (let i = 0; i < r.out.length; i++)
        if (r.out[i] !== 0) { first = i >> 1; break; }

    const applied = Math.floor(1000 / r.windowlen) * r.windowlen;
    const heard = applied + r.windowlen;

    if (first < heard || first >= heard + r.windowlen)
        fail(`stamp: a note at frame 1000 first sounded at ${first}, not in ` +
             `the window ${heard}-${heard + r.windowlen - 1}`);
    else
        process.stdout.write(`ok    stamp          frame 1000 -> applied at ` +
                             `${applied}, first sound at ${first}\n`);
}

/* Played keys (tw_keys): a key is recorded where it is played, and one
 * played in the window a rewind lands in, ahead of it, is the old run's
 * and goes with it -- the page would draw it at the old run's time on the
 * new run's roll. */
{
    const M = await createThinkWeb({ print: () => {}, printErr: () => {} });

    M._tw_create(RATE, 256, 128);

    const keys = () =>
    {
        const base = M._tw_keys() >>> 0;

        return Array.from({ length: M._tw_key_count() }, (_, i) =>
            [M.HEAP32[(base + i * 24 + 12) >> 2],
             M.HEAP32[(base + i * 24 + 20) >> 2]]);
    };

    const render = () =>
    {
        for (let done = 0; done < 2048; done += 128)
            M._tw_render(128);
    };

    M._tw_note_on(1000, 0, 60, 100);
    render();

    const played = JSON.stringify(keys());
    const epoch = M._tw_epoch();

    M._tw_keys_clear();
    M._tw_note_off(4000, 0, 60);
    M._tw_transport(4000, 2, 0);
    M._tw_note_on(4000, 0, 62, 100);
    render();

    const after = JSON.stringify(keys());

    if (played !== '[[60,1]]' || M._tw_epoch() === epoch ||
        after !== '[[62,1]]')
        fail(`keys: played ${played}, then across a rewind ${after}`);
    else
        process.stdout.write('ok    keys           a key is recorded, and ' +
                             'one ahead of a rewind in its window is not\n');
}

/* MIDI out (twMidiOut): a piece whose instrument names a port, given a
 * port list that matches. The program change goes when it attaches; every
 * note, its off and a mapped chanarg come out as bytes for the page, each
 * stamped with the AudioContext time of its frame -- frameOf(at), in
 * microseconds -- and a stop is a flush record. With the ports taken away
 * the instrument is detached and plays its dsp. */
{
    const M = await createThinkWeb({ print: () => {}, printErr: () => {} });

    M._tw_create(RATE, 256, 128);
    M.ccall('tw_instrument', 'number', ['string', 'string'],
            ['organ0.dsp', fs.readFileSync(path.join(dspDir, 'organ0.dsp'),
                                           'utf8')]);

    const piece =
        'tempo 60;\n' +
        'instrument ext { midi "Surge"; midichannel = 3; midiprogram = 5;\n' +
        '    cc cutoff = 74 { min = 100; max = 1100; }; dsp "organ0.dsp"; };\n' +
        'chain notes { stage g gen::grid { notes = "C4"; steps = 1;\n' +
        '    rows = 1; cells = "x"; period = 1 beats; hold = 0.5 beats; };\n' +
        '    sink { instrument = ext; }; };\n' +
        'chain sweep { stage s gen::steps { values = "0 1";\n' +
        '    period = 1 beats; min = 100; max = 600; };\n' +
        '    sink { instrument = ext; chanarg = "cutoff"; }; };\n';

    const state = () => M.UTF8ToString(M._tw_instrument_midi_state(0));
    const msgs = [];

    const drainMidi = () =>
    {
        const base = M._tw_midiout_events() >>> 0;

        for (let i = 0; i < M._tw_midiout_count(); i++)
        {
            const at = base + i * 32;
            const len = M.HEAP32[(at + 20) >> 2];

            msgs.push({
                when: M.HEAPF64[at >> 3],
                kind: M.HEAP32[(at + 8) >> 2],
                channel: M.HEAP32[(at + 12) >> 2],
                port: M.HEAP32[(at + 16) >> 2],
                generation: M.HEAP32[(at + 28) >> 2],
                bytes: [0, 8, 16].slice(0, len)
                    .map((b) => (M.HEAP32[(at + 24) >> 2] >>> b) & 0xff),
            });
        }

        M._tw_midiout_clear();
    };

    const render = (frames) =>
    {
        for (let done = 0; done < frames; done += 128)
        {
            M._tw_render(128);
            drainMidi();
        }
    };

    const ok = M.ccall('tw_piece_load', 'number', ['string', 'number'],
                       [piece, 1]) !== 0;
    const before = state();

    const ports = (names, enabled, generation) =>
        M.ccall('tw_midiout_ports', 'number', ['string', 'number', 'number'],
                [names, enabled, generation]);

    ports('Dev A\nSurge XT Out', 1, 1);
    drainMidi();

    const on = state();
    const program = msgs.shift();

    M._tw_transport(0, 0, 0);
    render(Math.round(2.2 * RATE));

    const origin = M._tw_origin();
    const us = (at) => Math.round((origin + at * RATE) / RATE * 1e6);
    const of = (status, d1) =>
        msgs.filter((m) => m.kind === 0 && m.bytes[0] === status &&
                           m.bytes[1] === d1);

    const ons = of(0x92, 60), offs = of(0x82, 60), ccs = of(0xb2, 74);

    if (!ok)
        fail('midi out: the piece did not load');
    else if (!before.includes('no MIDI access') ||
             on !== 'on Surge XT Out, channel 3')
        fail(`midi out: state "${before}" then "${on}"`);
    else if (program === undefined ||
             JSON.stringify(program.bytes) !== '[194,4]' || program.port !== 1 ||
             program.generation !== 1)
        fail(`midi out: no program change on attach (${JSON.stringify(program)})`);
    else if (ons.length !== 3 || offs.length < 2 ||
             ons.some((m, k) => m.when !== us(k)) ||
             offs.slice(0, 2).some((m, k) => m.when !== us(k + 0.5)))
        fail('midi out: notes not stamped at their frames: ' +
             JSON.stringify({ origin, ons: ons.map((m) => m.when),
                              offs: offs.map((m) => m.when) }));
    else if (ccs.length < 2 || ccs[0].bytes[2] !== 0 || ccs[1].bytes[2] !== 64)
        fail(`midi out: cutoff as CC 74 ${JSON.stringify(ccs.map((m) => m.bytes))}`);
    else
    {
        /* The same list again, then the same device at another index: the
           instrument is left on it, not detached and attached again --
           which would end what it is sounding -- and only renumbered. */
        msgs.length = 0;
        const same = ports('Dev A\nSurge XT Out', 1, 2);
        const moved = ports('Surge XT Out\nDev B\nDev A', 1, 3);

        drainMidi();
        render(Math.round(1.1 * RATE));

        const next = msgs.find((m) => m.kind === 0 && m.bytes[0] === 0x92);

        if (same !== 0 || moved !== 0 || msgs.some((m) => m.kind !== 0) ||
            next === undefined || next.port !== 0 || next.generation !== 3)
            fail('midi out: an unchanged device was applied again, or not ' +
                 `renumbered: ${same} ${moved} ` +
                 JSON.stringify(msgs.map((m) => [m.kind, m.port,
                                                 m.generation])));

        msgs.length = 0;
        M._tw_transport(M._tw_frame(), 1, 0);
        render(1024);

        const flush = msgs.find((m) => m.kind === 1);

        ports('', 0, 4);
        drainMidi();

        const detach = msgs.find((m) => m.kind === 2);

        if (flush === undefined || detach === undefined)
            fail(`midi out: stop and losing access did not flush and detach ` +
                 JSON.stringify(msgs.map((m) => m.kind)));
        else if (!state().includes('no MIDI access'))
            fail(`midi out: back on its dsp, state "${state()}"`);
        else
            process.stdout.write(`ok    midi out       program, ${ons.length} ` +
                                 `notes stamped at their frames, cc, a moved ` +
                                 `port renumbered, flush, detach\n`);
    }
}

/* The page's sender (midiout.js), on a clock of its own: a fake MIDIAccess
 * whose output opens on its first send and says so, as a browser's does;
 * a clock that is the identity; and the pump driven by hand.
 *
 *   - a port opening is not a new list: what is queued stays queued, and
 *     the worklet is not told again;
 *   - a message for an older list is dropped;
 *   - a retrigger ends the key first; an off for a key another channel
 *     took over is not sent;
 *   - a flush drops what is queued and ends what was handed over, no
 *     earlier than it was to start. */
{
    const { MidiSender } = await import('./midiout.js');

    let now = 1000;
    const sent = [];
    let told = 0;
    const access = { inputs: new Map(), outputs: new Map(),
                     onstatechange: null };
    const plug = (id, name) =>
    {
        const port = {
            id, name, type: 'output', state: 'connected',
            connection: 'closed',
            send (bytes, at)
            {
                if (port.connection === 'closed')
                {
                    port.connection = 'open';
                    access.onstatechange?.({ port });
                }

                sent.push({ id, bytes: [...bytes], at });
            },
        };

        access.outputs.set(id, port);
        return port;
    };

    plug('a', 'Synth A');
    Object.defineProperty(globalThis, 'navigator', {
        value: { requestMIDIAccess: async () => access },
        configurable: true,
    });

    const sender = new MidiSender({
        clock: { perfAt: (s) => s * 1000 },
        now: () => now,
        onPorts: () => { told++; },
    });

    await sender.open();
    clearInterval(sender.timer);           /* pumped by hand below */

    const gen = sender.generation;
    const msg = (ms, bytes, channel = 0, generation = gen) =>
        ({ kind: 0, when: ms * 1000, channel, port: 0, generation, bytes });

    /* A note now, its off and the next note later: the first send opens
       the port. */
    sender.take([msg(1000, [0x90, 60, 100]), msg(1500, [0x80, 60, 64]),
                 msg(2000, [0x90, 62, 100]),
                 msg(1000, [0x90, 70, 100], 0, gen - 1)]);

    const queuedAfterOpen = sender.queue.length;
    const toldAfterOpen = told;

    for (now = 1005; now <= 2000; now += 5)
        sender.pump();

    /* Channel 1 retriggers 62, which channel 0 holds; channel 0's off for
       it is then stale. */
    now = 2100;
    sender.take([msg(2100, [0x90, 62, 90], 1), msg(2200, [0x80, 62, 64], 0)]);

    for (now = 2105; now <= 2200; now += 5)
        sender.pump();

    now = 2200;

    /* A note handed over for later, a flush before it starts. */
    sender.take([msg(2220, [0x90, 64, 100], 1), msg(2400, [0x90, 65, 100], 1)]);
    sender.flush(1);
    now = 3000;
    sender.pump();
    sender.close();

    const show = (m) => `${m.bytes.map((b) => b.toString(16)).join(' ')}@${m.at}`;
    const got = sent.map(show).join(', ');
    const want = ['90 3c 64@1000', '80 3c 40@1500', '90 3e 64@2000',
                  '80 3e 40@2100', '90 3e 5a@2100', '90 40 64@2220',
                  '80 3e 40@2200', '80 40 40@2221'].join(', ');

    if (queuedAfterOpen !== 2 || toldAfterOpen !== 1)
        fail(`midi sender: a port opening was taken for a new list ` +
             `(${queuedAfterOpen} queued, told ${toldAfterOpen} times)`);
    else if (got !== want)
        fail(`midi sender: sent ${got}\n      wanted ${want}`);
    else
        process.stdout.write('ok    midi sender    a port opening, an old ' +
                             'list, a retrigger, a stale off, a flush\n');
}

/* The pitch wheel: a chain's `bend' mapping goes as 14-bit pitch bend,
 * and a stop puts a bent wheel back to center -- after the flush record, so
 * the page's flush, which drops what is queued, does not drop it. */
{
    const M = await createThinkWeb({ print: () => {}, printErr: () => {} });

    M._tw_create(RATE, 256, 128);

    const piece =
        'tempo 60;\n' +
        'instrument ext { midi "Surge"; midichannel = 2;\n' +
        '    bend wheel { min = -1; max = 1; }; };\n' +
        'chain w { stage s gen::steps { values = "1";\n' +
        '    period = 1 beats; min = -1; max = 1; };\n' +
        '    sink { instrument = ext; chanarg = "wheel"; }; };\n';
    const msgs = [];
    const drain = () =>
    {
        const base = M._tw_midiout_events() >>> 0;

        for (let i = 0; i < M._tw_midiout_count(); i++)
        {
            const at = base + i * 32;
            const len = M.HEAP32[(at + 20) >> 2];

            msgs.push({
                kind: M.HEAP32[(at + 8) >> 2],
                bytes: [0, 8, 16].slice(0, len)
                    .map((b) => (M.HEAP32[(at + 24) >> 2] >>> b) & 0xff),
            });
        }

        M._tw_midiout_clear();
    };

    const ok = M.ccall('tw_piece_load', 'number', ['string', 'number'],
                       [piece, 1]) !== 0;

    M.ccall('tw_midiout_ports', 'number', ['string', 'number', 'number'],
            ['Surge XT', 1, 1]);
    M._tw_transport(0, 0, 0);

    for (let done = 0; done < RATE / 2; done += 128)
    {
        M._tw_render(128);
        drain();
    }

    M._tw_transport(M._tw_frame(), 1, 0);

    for (let done = 0; done < 1024; done += 128)
    {
        M._tw_render(128);
        drain();
    }

    /* Bent again and then detached -- the ports going -- with no stop in
       between: the detach centers it too. */
    const before = msgs.length;

    M._tw_transport(M._tw_frame(), 0, 0);

    for (let done = 0; done < RATE / 2; done += 128)
    {
        M._tw_render(128);
        drain();
    }

    M.ccall('tw_midiout_ports', 'number', ['string', 'number', 'number'],
            ['', 0, 2]);
    drain();

    const tail = msgs.slice(before);
    const detach = tail.findIndex((m) => m.kind === 2);
    const centered = tail.findIndex((m, i) => i > detach && m.kind === 0 &&
                                              m.bytes[0] === 0xe1 &&
                                              m.bytes[2] === 64);

    if (detach < 0 || centered < 0)
        fail('midi out: a detach left the wheel bent: ' +
             tail.map((m) => `${m.kind}:${m.bytes.join(' ')}`).join(', '));

    const shown = msgs.map((m) => `${m.kind}:${m.bytes.join(' ')}`).join(', ');
    const up = msgs.findIndex((m) => m.kind === 0 && m.bytes[0] === 0xe1 &&
                                     m.bytes[1] === 127 && m.bytes[2] === 127);
    const flush = msgs.findIndex((m) => m.kind === 1);
    const center = msgs.findIndex((m) => m.kind === 0 && m.bytes[0] === 0xe1 &&
                                         m.bytes[1] === 0 && m.bytes[2] === 64);

    if (!ok || up < 0 || flush < up || center < flush)
        fail(`midi out: the wheel up and back to center after a stop: ${shown}`);
    else
        process.stdout.write('ok    midi out       a bend is the pitch wheel, ' +
                             'centered again after the flush on stop\n');
}

/* MIDI clock in the browser module: asked for, Start and then a tick
 * every 24th of a beat stamped at its frame's context time, Stop with the
 * transport; not asked for, none. */
{
    const M = await createThinkWeb({ print: () => {}, printErr: () => {} });

    M._tw_create(RATE, 256, 128);

    const recs = [];
    const drain = () =>
    {
        const base = M._tw_midiout_events() >>> 0;

        for (let i = 0; i < M._tw_midiout_count(); i++)
        {
            const at = base + i * 32;
            const len = M.HEAP32[(at + 20) >> 2];

            recs.push({
                when: M.HEAPF64[at >> 3],
                kind: M.HEAP32[(at + 8) >> 2],
                bytes: [0, 8, 16].slice(0, len)
                    .map((b) => (M.HEAP32[(at + 24) >> 2] >>> b) & 0xff),
            });
        }

        M._tw_midiout_clear();
    };
    const render = (frames) =>
    {
        for (let done = 0; done < frames; done += 128)
        {
            M._tw_render(128);
            drain();
        }
    };

    M.ccall('tw_piece_load', 'number', ['string', 'number'],
            ['tempo 120;\nchain c { stage s gen::eno_line { };' +
             ' sink { channel = 1; }; };\n', 1]);
    M._tw_midiout_clock(1);
    M._tw_transport(0, 0, 0);
    render(RATE / 2);
    M._tw_transport(M._tw_frame(), 1, 0);
    render(1024);

    const origin = M._tw_origin();
    const clock = recs.filter((r) => r.kind === 3);
    const ticks = clock.filter((r) => r.bytes[0] === 0xf8);
    const exact = ticks.every((r, k) =>
        r.when === Math.round((origin + k * RATE / 48) / RATE * 1e6));

    M._tw_midiout_clock(0);
    recs.length = 0;
    M._tw_transport(M._tw_frame(), 0, 0);
    render(RATE / 4);

    if (clock[0]?.bytes[0] !== 0xfa || clock.at(-1)?.bytes[0] !== 0xfc ||
        ticks.length < 23 || !exact)
        fail(`midi clock: ${clock.length} records, ${ticks.length} ticks, ` +
             `exact ${exact}`);
    else if (recs.some((r) => r.kind === 3))
        fail('midi clock: sent with nobody asking for it');
    else
        process.stdout.write(`ok    midi clock     start, ${ticks.length} ticks ` +
                             'at their frames, stop\n');
}

/* The page's sender: a clock record goes to the outputs checked for clock,
   as it is, and to no other. */
{
    const { MidiSender } = await import('./midiout.js');
    const sent = [];
    const out = (id) => ({ id, name: id, type: 'output', state: 'connected',
                           send: (bytes, at) => sent.push({ id, bytes: [...bytes],
                                                            at }) });
    const access = { inputs: new Map(), outputs: new Map([['a', out('a')],
                                                          ['b', out('b')]]),
                     onstatechange: null };

    Object.defineProperty(globalThis, 'navigator', {
        value: { requestMIDIAccess: async () => access }, configurable: true,
    });

    const sender = new MidiSender({ clock: { perfAt: (s) => s * 1000 },
                                    now: () => 1000 });

    await sender.open();
    clearInterval(sender.timer);
    sender.setClock(['b']);
    sender.take([{ kind: 3, when: 1000 * 1000, channel: -2, port: -1,
                   generation: 0, bytes: [0xf8] }]);
    sender.close();

    if (sent.length !== 1 || sent[0].id !== 'b' || sent[0].bytes[0] !== 0xf8 ||
        sent[0].at !== 1000)
        fail(`midi sender: clock went ${JSON.stringify(sent)}`);
    else
        process.stdout.write('ok    midi sender    clock to the checked output only\n');
}

process.stdout.write(`\n${failures === 0 ? 'all passed' : failures + ' failed'}\n`);
process.exitCode = failures;
