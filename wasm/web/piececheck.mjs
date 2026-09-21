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
 * piececheck.mjs -- every seeded piece, composed by the browser build and
 * held against what genwav composes.
 *
 *   node wasm/web/piececheck.mjs [BUILD_DIR] [NODE_BUILD_DIR]
 *
 * The tape gate, without a browser. For each shipped piece that pins a seed,
 * genwav.mjs renders it under Node out of wasm/'s module -- libthink and the
 * plugins as side modules, the scheduler stepped by a fixed virtual clock in
 * windows of 1024 at 44.1 kHz -- and the browser's module composes the same
 * seconds with the scheduler stepped by the audio clock in windows of 256 and
 * of 128, at 48 kHz and at 44.1. The tapes must be the same tape.
 *
 * Two gates in one, which is why it is one comparison. That two different
 * wasm builds of the same tree agree is the wasm-against-wasm gate: nothing
 * about linking every plugin in rather than dlopening it may change what a
 * piece composes. That four different step sizes agree is the
 * step-invariance gate: what a piece is must be a function of the file and
 * the seed and not of the host's buffer, or two peers with different sound
 * cards could not play the same piece (docs/JAM.md).
 *
 * The pieces that pin no seed are skipped and named. They draw their master
 * seed from the host at load and are not meant to repeat -- there is no
 * tape for them to have.
 *
 * Then the other command, which no tape comparison can reach: a key. A
 * seeded piece composes the same whoever is listening, so nothing above
 * says whether `input midi' arrives. gen/hands.gen is the piece that is
 * nothing but input -- three chains, no generators, no instruments -- and
 * a check here holds a chord down in it and listens.
 *
 * And then the thing no tape comparison can reach either, for the
 * opposite reason: whether any of it makes a sound. A tape is what the
 * scheduler delivered, and a piece whose sinks name channels nothing is
 * loaded on delivers every note of it into silence -- fern composes three
 * and a half thousand notes in two minutes at a peak of 0.000, and its
 * tape is perfect. So the last check plays every shipped piece the way
 * the page plays it, the defaults aimed at the channels the piece left to
 * the reader, and asks for a peak. A shipped piece that is silent under
 * the page's defaults fails the build, which is the property the report
 * that started all this was missing.
 *
 * What this cannot see is the browser: the worklet, its messages, and the
 * quanta a real audio thread asks for. browsertest.mjs runs the same tape
 * comparison through Chromium and Firefox.
 *
 * Exit status is the number of failures.
 */

import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync, spawn } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';

import { seeded, tapeBefore } from '../tape.mjs';
import { defaultFor } from './patch.js';
import { playAimed, playAt, playPiece } from './render.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const top = path.join(here, '..', '..');

/* The two build directories stay positional, as they were. `-j' and
   `--shard' are this file's own and are taken out of the way first, so a
   caller that passes neither sees the argument list it always saw -- which
   includes browsertest.mjs, since importing this module runs what is
   below against its process.argv. */
const flags = { jobs: Math.max(1, os.availableParallelism()), shard: null };
const positional = [];

for (let i = 2; i < process.argv.length; i++)
{
    const a = process.argv[i];
    const shard = a === '--shard' && i + 1 < process.argv.length
        ? /^(\d+)\/(\d+)$/.exec(process.argv[i + 1]) : null;

    if (shard !== null)
    {
        flags.shard = { index: Number(shard[1]), count: Number(shard[2]) };
        i++;
    }
    else if ((a === '-j' || a === '--jobs') && i + 1 < process.argv.length &&
             /^[1-9]\d*$/.test(process.argv[i + 1]))
        flags.jobs = Number(process.argv[++i]);
    else
        positional.push(a);
}

const build = path.resolve(positional[0] ?? path.join(top, 'build-web'));
const nodeBuild = path.resolve(positional[1] ??
                               process.env.THINK_WASM_BUILD ??
                               path.join(top, 'build-wasm'));

/* How much of each piece. A minute is what the step-size fix was measured
   over and is long enough for the slowest shipped piece to have said
   something; tide delivers a couple of thousand events in it. */
export const SECONDS = 60;

/* The rate and window each run uses. The first is the page's own; the rest
   are what has to agree with it. */
export const SHAPES = [
    { rate: 48000, windowlen: 256 },
    { rate: 48000, windowlen: 128 },
    { rate: 44100, windowlen: 256 },
    { rate: 44100, windowlen: 128 },
];

/* The .dsp files a piece's instruments are looked up among -- the same ones
   the page hands the worklet before it loads anything. */
export function instruments (buildDir)
{
    const dir = path.join(buildDir, 'dsp');
    const out = {};

    for (const name of JSON.parse(
             fs.readFileSync(path.join(dir, 'index.json'), 'utf8')))
    {
        /* The index carries the kit as well, and a wav read as utf8 is
           not a wav any more -- every byte that is not valid UTF-8 comes
           back as U+FFFD. Those go through samples() below, as bytes. */
        if (name.startsWith('samples/'))
            continue;

        out[name] = fs.readFileSync(path.join(dir, name), 'utf8');
    }

    return out;
}

/* And the wavs, as bytes, keyed by the same index name so that what
   reaches tw_sample is the path osc::sample's `file' resolves to. */
export function samples (buildDir)
{
    const dir = path.join(buildDir, 'dsp');
    const out = {};

    for (const name of JSON.parse(
             fs.readFileSync(path.join(dir, 'index.json'), 'utf8')))
        if (name.startsWith('samples/'))
            out[name] = new Uint8Array(fs.readFileSync(path.join(dir, name)));

    return out;
}

/* Every shipped piece, and whether it pins a seed. */
export function pieces (buildDir)
{
    const dir = path.join(buildDir, 'gen');

    return JSON.parse(fs.readFileSync(path.join(dir, 'index.json'), 'utf8'))
        .map((name) =>
        {
            const text = fs.readFileSync(path.join(dir, name), 'utf8');

            return { name, text, seeded: seeded(text) };
        });
}

/* genwav.mjs's tape for a piece, cut where the comparison ends. The
 * reference every gate here is held against: a different module, a
 * different loader, a different step.
 *
 * `commands' are stamped commands in the shape that crosses the network
 * (commands.js) -- knobs, tempos, the stop -- turned into genwav's `-c'
 * argv, so that the peers and the reference are given one command stream.
 * `knobs' maps a command's knob index, which is the module's numbering
 * over every knob a piece declared, to the name genwav takes; a piece with
 * a hidden knob before a shown one numbers them differently from the
 * sliders on the page, so the map is by index and not by position.
 *
 * `stopAt' is the transport time the tape is cut at, and genwav is asked
 * for a few seconds past it so that the stop itself is on what comes back.
 * Without one the whole of `seconds' is taken. */
export function reference (name, nodeBuildDir,
                           { commands = [], knobs = {}, stopAt = null,
                             seconds = SECONDS } = {})
{
    const until = stopAt === null ? seconds : stopAt + 5;

    /* To stdout rather than to a file, so two of these gates running at
       once -- this one and browsertest.mjs, say -- cannot read each
       other's tapes. */
    const args = [path.join(here, '..', 'genwav.mjs'),
                  '-s', String(until), '-t', '-', '-q'];

    for (const c of commands)
    {
        if (c.type === 'knob')
        {
            if (knobs[c.knob] === undefined)
                throw new Error(`reference: knob ${c.knob} of ${name} has ` +
                                'no name to give genwav');

            args.push('-c', `${c.at} knob ${knobs[c.knob]} ${c.value}`);
        }
        else if (c.op === 'tempo')
            args.push('-c', `${c.at} tempo ${c.bpm}`);
        else if (c.op === 'stop')
            args.push('-c', `${c.at} stop`);
    }

    args.push(path.join(top, 'gen', name));

    let text;

    try
    {
        text = execFileSync('node', args,
                            { cwd: top, encoding: 'utf8',
                              env: { ...process.env,
                                     THINK_WASM_BUILD: nodeBuildDir } });
    }
    catch (e)
    {
        /* genwav.mjs's statuses, which are scripts/genwav's: 4 is a voice the
           engine's guard dropped for going non-finite, 3 is a render that
           reached full scale. Either way what came back is a report of
           something other than the piece, so it is not a tape to hold
           anything against -- and an execFileSync that merely threw said only
           that a command had failed. */
        const why = e.status === 4
            ? 'a voice went non-finite (genwav exit 4)'
            : e.status === 3
              ? 'the render clipped (genwav exit 3)'
              : `genwav exited ${e.status === undefined ? '?' : e.status}`;

        throw new Error(`reference: ${name}: ${why}` +
                        `${e.stderr ? `\n${e.stderr}` : ''}`);
    }

    return tapeBefore(text, stopAt === null ? seconds : stopAt);
}

/* Where the two first differ, for a failure that can be acted on. */
export function firstDifference (want, got)
{
    const a = want.split('\n'), b = got.split('\n');
    let i = 0;

    while (i < a.length && i < b.length && a[i] === b[i])
        i++;

    return `line ${i + 1}: genwav has ${a[i] ? `[${a[i]}]` : 'nothing'}, ` +
           `this has ${b[i] ? `[${b[i]}]` : 'nothing'}` +
           ` (${a.length - 1} events against ${b.length - 1})`;
}

/* A chord held down in gen/hands.gen.
 *
 * Its first chain is `input midi' into a gen::arp with `pass = 0': the
 * chord it is handed is eaten, and what comes out is a figure walking up
 * through the held notes and their octave, one step every `period'. So a
 * press that arrives produces notes that are *not* the notes pressed, which
 * is what makes this worth asserting -- a chord passed straight through
 * would look like success and mean the arp never saw it.
 *
 * The piece declares no instruments, so a patch goes on the channel first;
 * without one the figure is composed and nothing sounds.
 */
async function checkKeys (createThinkWeb, dsps, kit, all)
{
    const piece = all.find((p) => p.name === 'hands.gen');

    if (piece === undefined)
    {
        process.stdout.write('FAIL  hands.gen is not in this build\n');
        return 1;
    }

    const CHANNEL = 0;              /* the engine's: the file writes 1 */
    const CHORD = [53, 56, 60];     /* F3 Ab3 C4, and hands.gen is F minor */
    const HELD = 4;

    const r = await playAt(createThinkWeb, {
        gen: piece.text,
        instruments: dsps,
        samples: kit,
        patches: { [CHANNEL]: dsps['rpiano0.dsp'] },
        keys: [
            ...CHORD.map((note) => ({ at: 1, channel: CHANNEL, note,
                                      velocity: 100 })),
            ...CHORD.map((note) => ({ at: 1 + HELD, channel: CHANNEL, note })),
        ],
        seconds: 1 + HELD + 1,
    });

    if (!r.ok)
    {
        process.stdout.write(`FAIL  hands.gen did not load: ` +
                             `${r.errors.join('; ')}\n`);
        return 1;
    }

    const notes = r.events.filter((e) => e.kind === 'N' &&
                                         e.channel === CHANNEL);

    /* Every step of the figure is a held note or one of them an octave up:
       `octaves = 2' in the piece. */
    const reachable = new Set([...CHORD, ...CHORD.map((n) => n + 12)]);
    const stray = notes.filter((e) => !reachable.has(e.note));

    /* One step every `period', which the piece sets to the Step knob's
       default of 0.12 s. Held for HELD seconds, so: about thirty. Asserted
       loosely, because the exact count is a function of where the press
       landed in a window and the figure is the point, not the arithmetic. */
    const want = HELD / 0.12;
    const complaints = [];

    if (notes.length < want * 0.8 || notes.length > want * 1.2)
        complaints.push(`${notes.length} steps in ${HELD} s of held chord, ` +
                        `not about ${Math.round(want)}`);

    if (stray.length > 0)
        complaints.push(`${stray.length} steps on notes nobody held ` +
                        `(${stray.slice(0, 4).map((e) => e.note).join(', ')})`);

    /* The chord itself must not be among them: `pass = 0' eats it, and an
       arp that passed the press through would be an arp that never held
       it. The figure's steps carry a duration; a passed-through press does
       not. */
    if (notes.some((e) => e.duration === 0))
        complaints.push('a press was passed straight through; ' +
                        'the arp did not eat it');

    if (r.peak === 0)
        complaints.push('the figure was composed but nothing sounded');

    if (complaints.length > 0)
    {
        process.stdout.write(`FAIL  hands.gen     a held chord: ` +
                             `${complaints.join('; ')}\n`);
        return 1;
    }

    process.stdout.write(
        `ok    hands.gen      a chord held ${HELD} s became ` +
        `${notes.length} steps, peak ${r.peak.toFixed(3)}\n`);

    return 0;
}

/* What the page would put on a channel a piece named and aimed at nothing
 * of its own: gthPrefs.cpp's first-run patch for that channel, read out of
 * the build's patches/ and handed over whole.
 *
 * The bytes and not a reading of them. The module reads a .patch with the
 * same code the application does (src/PatchFile.h), so there is nothing
 * left here that could pass on a reading the page does not have -- which is
 * what this used to be careful about by calling patch.js's parser.
 *
 * Returns null for a patch this build does not ship. One whose .dsp the
 * build does not ship is refused by the module instead; playAimed hands
 * both kinds of channel back in `unaimed' and checkAudible names them as a
 * build to fix rather than a piece to blame.
 */
export function defaults (buildDir)
{
    return (channel) =>
    {
        const name = defaultFor(channel);
        const file = path.join(buildDir, 'patches', name);

        if (!fs.existsSync(file))
            return null;

        return { name, text: fs.readFileSync(file, 'utf8') };
    };
}

/* How loud counts as heard: -60 dBFS. render.mjs says why there is a
   floor at all and why this one. */
const FLOOR = 0.001;

/* Every shipped piece, played the way the page plays it, held to a peak.
 *
 * Not a tape: a tape says what was composed and this says whether it was
 * audible, and the two fail apart -- fern's tape is perfect and fern was
 * silent. A minute is the window, and the run stops as soon as the piece
 * has been heard; one that has not been heard in a minute of transport is
 * not going to be saved by the rest of it.
 */
async function checkAudible (createThinkWeb, dsps, kit, all, buildDir)
{
    const patchFor = defaults(buildDir);
    let failures = 0;

    for (const piece of all)
    {
        const r = await playAimed(createThinkWeb,
                                  { gen: piece.text, instruments: dsps,
                                    samples: kit,
                                    patchFor, seconds: SECONDS,
                                    floor: FLOOR });

        if (!r.ok)
        {
            process.stdout.write(`FAIL  ${piece.name.padEnd(14)} did not ` +
                                 `load: ${r.errors.join('; ')}\n`);
            failures++;
            continue;
        }

        /* Said whether or not the piece was audible: a build missing the
           patches the defaults name is a broken build, and a piece that
           sounded anyway does not make it less broken. */
        if (r.unaimed.length > 0)
            process.stdout.write(
                `      ${' '.repeat(14)} this build ships no patch for ` +
                `channel ${r.unaimed.map((c) => c + 1).join(', ')} -- ` +
                `${[...new Set(r.unaimed.map(defaultFor))].join(', ')} ` +
                'is not under patches/; fix the build, not the piece\n');

        if (r.peak <= FLOOR)
        {
            const how = r.aimed.length > 0
                ? `aimed ${r.aimed.map((a) => `${a.channel + 1} at ` +
                                              a.patch).join(', ')}`
                : r.unaimed.length > 0
                ? 'and nothing could be aimed at it'
                : 'it names no channel the page could aim';

            process.stdout.write(
                `FAIL  ${piece.name.padEnd(14)} nothing above ` +
                `${r.peak.toExponential(1)} through ${SECONDS} s under the ` +
                `page's defaults; ${how}\n`);
            failures++;
            continue;
        }

        const how = r.aimed.length === 0
            ? 'its own instruments'
            : r.aimed.map((a) => `${a.channel + 1}=${a.patch}`).join(' ');

        process.stdout.write(
            `ok    ${piece.name.padEnd(14)} heard by ${r.at.toFixed(1)} s   ` +
            `${how}\n`);
    }

    return failures;
}

/* The pieces this process is responsible for. Every Nth from I, rather
   than a contiguous block: the pieces differ wildly in how long they take
   to compose -- anthem delivers four thousand events where hands delivers
   none -- and striding spreads the slow ones over the shards instead of
   landing them all on one.

   hands.gen's own check is not sharded. It is one fixed piece, and a shard
   that did not draw it would report it missing rather than skip it, so
   shard 0 does it and the rest leave it alone. */
function mine (all, shard)
{
    return shard === null
        ? all
        : all.filter((_, i) => i % shard.count === shard.index);
}

/* One process's share of the work: the tape comparison, then hands.gen,
   then the peak. The three phases and their output are what they were --
   what changes is only which pieces reach them. */
async function runShard (shard)
{
    const { default: createThinkWeb } =
        await import(pathToFileURL(path.join(build, 'thinkweb.js')).href);

    const dsps = instruments(build);
    const kit = samples(build);
    const all = pieces(build);
    let failures = 0;

    for (const piece of mine(all, shard))
    {
        if (!piece.seeded)
        {
            process.stdout.write(`skip  ${piece.name.padEnd(14)} pins no ` +
                                 'seed; it is not meant to repeat\n');
            continue;
        }

        /* A reference that could not be taken fails this piece, not the
           run. */
        let want;

        try
        {
            want = reference(piece.name, nodeBuild);
        }
        catch (e)
        {
            failures++;
            process.stdout.write(`FAIL  ${piece.name.padEnd(14)} ` +
                                 `${e.message.split('\n')[0]}\n`);
            continue;
        }

        const cells = [];

        for (const shape of SHAPES)
        {
            const r = await playPiece(createThinkWeb,
                                      { ...shape, gen: piece.text,
                                        instruments: dsps,
                                        samples: kit,
                                        seconds: SECONDS });
            const at = `${shape.rate / 1000}k/${shape.windowlen}`;

            if (!r.ok)
            {
                failures++;
                cells.push(`${at} did not load: ${r.errors.join('; ')}`);
                continue;
            }

            const got = tapeBefore(r.tape, SECONDS);

            if (got === want)
                cells.push(`${at} ok`);
            else
            {
                failures++;
                cells.push(`${at} DIFFERS -- ${firstDifference(want, got)}`);
            }
        }

        const bad = cells.some((c) => !c.endsWith('ok'));

        process.stdout.write(
            `${bad ? 'FAIL' : 'ok  '}  ${piece.name.padEnd(14)} ` +
            `${String(want.split('\n').length - 1).padStart(5)} events   ` +
            `${cells.join('   ')}\n`);
    }

    if (shard === null || shard.index === 0)
        failures += await checkKeys(createThinkWeb, dsps, kit, all);

    if (shard === null)
        process.stdout.write('\n');

    failures += await checkAudible(createThinkWeb, dsps, kit,
                                   mine(all, shard), build);

    return failures;
}

/* The shards, run at once, each in its own process -- which is what it
   takes: the module is wasm on one thread, so two pieces composed in one
   process take exactly as long as one after the other.

   Every line a shard writes is prefixed with the shard that wrote it.
   Whose line it is matters here in a way it did not when there was one
   writer: the shards finish their pieces at their own pace and the output
   interleaves. Lines are re-assembled before they are prefixed, since a
   child's stdout arrives in chunks that do not respect them. */
function runFanOut (count)
{
    const children = Array.from({ length: count }, (_, i) =>
    {
        const child = spawn(process.execPath,
                            [fileURLToPath(import.meta.url), build, nodeBuild,
                             '--shard', `${i}/${count}`],
                            { stdio: ['ignore', 'pipe', 'inherit'] });
        const tag = `[${String(i + 1).padStart(String(count).length)}] `;
        let rest = '';

        child.stdout.setEncoding('utf8');
        child.stdout.on('data', (d) =>
        {
            const parts = (rest + d).split('\n');

            rest = parts.pop();

            for (const line of parts)
                process.stdout.write(tag + line + '\n');
        });

        return new Promise((resolve, reject) =>
        {
            child.on('error', reject);
            child.on('close', (status) =>
            {
                if (rest !== '')
                    process.stdout.write(tag + rest + '\n');

                resolve(status);
            });
        });
    });

    return Promise.all(children);
}

if (import.meta.url === pathToFileURL(process.argv[1]).href)
{
    if (!fs.existsSync(path.join(nodeBuild, 'thinksynth.mjs')))
    {
        process.stdout.write(
            `piececheck: no Node module in ${nodeBuild}. It is the tape ` +
            'everything here is compared against;\n            build it ' +
            'first -- see the top of wasm/CMakeLists.txt.\n');
        process.exit(1);
    }

    /* A shard reports its own failures as its exit status and says nothing
       about the run as a whole; the parent adds them up and does the
       talking. `-j 1' is the old single process, output and all. */
    let failures;

    if (flags.shard === null && flags.jobs > 1)
    {
        const statuses = await runFanOut(
            Math.min(flags.jobs, pieces(build).length));

        /* A shard killed by a signal, or one that threw before it could
           count anything, exits non-zero without a tally to add: it is at
           least one failure, and saying so is better than reporting none. */
        failures = statuses.reduce((n, s) => n + (s === null ? 1 : s), 0);
    }
    else
        failures = await runShard(flags.shard);

    if (flags.shard === null)
        process.stdout.write(
            `\n${failures === 0
                 ? 'every seeded piece composes the same tape in the browser ' +
                   'build as in genwav, at every step; a chord held in ' +
                   'hands.gen is arpeggiated; and every shipped piece sounds ' +
                   "under the page's defaults\n"
                 : `${failures} failed\n`}`);

    process.exitCode = failures;
}
