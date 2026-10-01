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

    M.ccall('tw_midiout_ports', 'number', ['string', 'number'],
            ['Dev A\nSurge XT Out', 1]);
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
             JSON.stringify(program.bytes) !== '[194,4]' || program.port !== 1)
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
        msgs.length = 0;
        M._tw_transport(M._tw_frame(), 1, 0);
        render(1024);

        const flush = msgs.find((m) => m.kind === 1);

        M.ccall('tw_midiout_ports', 'number', ['string', 'number'], ['', 0]);
        drainMidi();

        const detach = msgs.find((m) => m.kind === 2);

        if (flush === undefined || detach === undefined)
            fail(`midi out: stop and losing access did not flush and detach ` +
                 JSON.stringify(msgs.map((m) => m.kind)));
        else if (!state().includes('no MIDI access'))
            fail(`midi out: back on its dsp, state "${state()}"`);
        else
            process.stdout.write(`ok    midi out       program, ${ons.length} ` +
                                 `notes stamped at their frames, cc, flush, ` +
                                 `detach\n`);
    }
}

process.stdout.write(`\n${failures === 0 ? 'all passed' : failures + ' failed'}\n`);
process.exitCode = failures;
