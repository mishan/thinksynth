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
 * genwav.mjs -- scripts/genwav.cpp, under Node, on the WebAssembly build.
 *
 *   node wasm/genwav.mjs -s 180 -o ebb.wav gen/ebb.gen
 *   node wasm/genwav.mjs -t - gen/round.gen | head
 *
 * The same options and the same output as the native tool, and
 * wasm/compare.mjs renders a piece with both and says where they part. The
 * loop, the tail, the level summary and the WAV writer are genwav.cpp's,
 * transcribed; the synth, the scheduler and every plugin are the real ones,
 * compiled to wasm -- see thinkwasm.cpp for the seam and wasm/CMakeLists.txt
 * for the build.
 *
 * The native build is glibc and libstdc++, this one musl and libc++, and
 * nothing a piece does may depend on which: the scheduler breaks ties by
 * push order, the composers draw through thcRandom.h and sort stably, and
 * osc::static carries its own generator. What is left is libm, whose last
 * bit the two do not always agree on -- now and then a sample lands one
 * step away in the 16-bit output, and that is the whole difference.
 *
 * A transcription has two things to get right that the C gets for free.
 * genwav.cpp's arithmetic on samples is float arithmetic, and a double
 * standing in for a float rounds differently: Math.fround is where the C
 * has a float. And printf rounds an exact tie to even where toFixed rounds
 * it away from zero, which on a tape of beat-aligned times happens often
 * enough to see -- fixed() below is printf's rounding.
 *
 * -p defaults to the build tree's plugins rather than an install prefix,
 * since nothing installs these. THINK_WASM_BUILD names another build tree.
 */

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

import { drain as drainTape, fixed, tapeLine } from './tape.mjs';

/* genwav.cpp's TAIL_SILENT and TAIL_MAX, and its clip threshold -- the
   float ones as the floats they are. */
const TAIL_SILENT = Math.fround(1e-4);
const TAIL_MAX    = 8.0;
const CLIP_LEVEL  = Math.fround(0.999);

const here = path.dirname(fileURLToPath(import.meta.url));
const buildDir = process.env.THINK_WASM_BUILD ??
    path.join(here, '..', 'build-wasm');

function usage (argv0)
{
    process.stdout.write(
        `usage: ${argv0} [-p PATH] [-s SECONDS] [-o FILE.wav] [-t FILE] file.gen\n` +
        '\n' +
        '  -p, --plugin-path PATH  where to find plugin .so files\n' +
        '  -s, --seconds N         how long to run the transport (default 120)\n' +
        '  -o, --output FILE       write the audio here, 16-bit PCM WAV\n' +
        '  -t, --tape FILE         write the delivered events here (- for stdout)\n' +
        '  -q, --quiet             no summary\n');
}

/* lrintf: to nearest, ties to even. Math.round sends every tie up. */
function lrint (v)
{
    const r = Math.round(v);

    return (r - v === 0.5 && r % 2 !== 0) ? r - 1 : r;
}

/* genwav.cpp's writeWav, clamp and all. */
function wavBytes (windows, samples, channels, rate)
{
    const dataBytes = samples * 2;
    const b = Buffer.alloc(44 + dataBytes);
    const d = new DataView(b.buffer, b.byteOffset, b.length);

    b.write('RIFF', 0, 'latin1');
    d.setUint32(4, (36 + dataBytes) >>> 0, true);
    b.write('WAVE', 8, 'latin1');
    b.write('fmt ', 12, 'latin1');
    d.setUint32(16, 16, true);
    d.setUint16(20, 1, true);
    d.setUint16(22, channels, true);
    d.setUint32(24, rate, true);
    d.setUint32(28, rate * channels * 2, true);
    d.setUint16(32, channels * 2, true);
    d.setUint16(34, 16, true);
    b.write('data', 36, 'latin1');
    d.setUint32(40, dataBytes >>> 0, true);

    let o = 44;

    for (const w of windows)
    {
        for (let v of w)
        {
            if (v > 1)  v = 1;
            if (v < -1) v = -1;

            d.setInt16(o, lrint(Math.fround(v * 32767)), true);
            o += 2;
        }
    }

    return b;
}

async function main (argv0, args)
{
    let pluginPath = path.join(buildDir, 'plugins');
    let genFile = '', wavFile = '', tapeFile = '';
    let seconds = 120;
    let quiet = false;

    for (let i = 0; i < args.length; i++)
    {
        const a = args[i];

        if (a === '-p' || a === '--plugin-path')
        {
            if (++i >= args.length) { usage(argv0); return 2; }
            pluginPath = args[i];
        }
        else if (a === '-s' || a === '--seconds')
        {
            if (++i >= args.length) { usage(argv0); return 2; }
            seconds = parseFloat(args[i]) || 0;     /* atof */
        }
        else if (a === '-o' || a === '--output')
        {
            if (++i >= args.length) { usage(argv0); return 2; }
            wavFile = args[i];
        }
        else if (a === '-t' || a === '--tape')
        {
            if (++i >= args.length) { usage(argv0); return 2; }
            tapeFile = args[i];
        }
        else if (a === '-q' || a === '--quiet')
            quiet = true;
        else if (a === '-h' || a === '--help')
        {
            usage(argv0);
            return 0;
        }
        else if (genFile === '')
            genFile = a;
        else
        {
            usage(argv0);
            return 2;
        }
    }

    if (genFile === '' || seconds <= 0)
    {
        usage(argv0);
        return 2;
    }

    if (wavFile === '' && tapeFile === '' && quiet)
    {
        process.stderr.write(`${argv0}: nothing to write and nothing to say\n`);
        return 2;
    }

    if (!pluginPath.endsWith('/'))
        pluginPath += '/';

    let createThink;

    try
    {
        ({ default: createThink } = await import(
            pathToFileURL(path.join(buildDir, 'thinksynth.mjs')).href));
    }
    catch (e)
    {
        process.stderr.write(`${argv0}: no wasm build under ${buildDir} -- ` +
                             `see wasm/CMakeLists.txt (${e.message})\n`);
        return 2;
    }

    /* The environment is Emscripten's own unless it is handed ours, and the
       dsp/ search reads THINK_DSP_PATH from it.

       And one variable of the module's own. MAIN_MODULE=1 links every
       system library whole, Emscripten's compiler-rt carries LLVM's
       profiling runtime, and that runtime's constructor truncates a
       default.profraw into the working directory on every run -- an empty
       file left wherever genwav.mjs was run from. This is the runtime's own
       "already done" flag, so it skips the truncate; the write it would do
       at exit never comes, since the module never runs its atexit list. */
    const M = await createThink({
        preRun: [ (m) =>
        {
            Object.assign(m.ENV, process.env);
            m.ENV.__LLVM_PROFILE_RT_INIT_ONCE = '1';
        } ],
    });

    if (M.ccall('tw_open', 'number', ['string'], [pluginPath]) === 0)
    {
        process.stderr.write(`${argv0}: no composer modules under ` +
                             `${pluginPath} -- build the plugins first, or ` +
                             'pass -p\n');
        return 2;
    }

    if (!M.ccall('tw_load', 'number', ['string'], [genFile]))
    {
        for (let k = 0; k < M._tw_error_count(); k++)
            process.stderr.write(M.UTF8ToString(M._tw_error(k)) + '\n');

        return 1;
    }

    let tape = null;

    if (tapeFile === '-')
        tape = 1;
    else if (tapeFile !== '')
    {
        try
        {
            tape = fs.openSync(tapeFile, 'w');
        }
        catch
        {
            process.stderr.write(`${argv0}: cannot write ${tapeFile}\n`);
            return 1;
        }
    }

    let notes = 0;
    let tapeText = '';

    /* genwav.cpp's sigDelivered handler, run after the fact: the events a
       step delivered are queued on the wasm side and taken here. */
    const drain = () =>
    {
        for (const e of drainTape(M))
        {
            if (e.kind === 'N')
                notes++;

            if (tape !== null)
                tapeText += tapeLine(e);
        }

        if (tape !== null && tapeText.length > 65536)
        {
            fs.writeSync(tape, tapeText);
            tapeText = '';
        }
    };

    const channels = M._tw_channels();
    const window = M._tw_window();
    const rate = M._tw_rate();
    const dt = window / rate;
    const frame = channels * window;

    const windows = [];

    /* One window of audio per step of the clock, copied out of the heap
       before the next one overwrites it -- and interleaved on the way, as
       genwav.cpp does, since the synth's window is planar. */
    const renderWindow = () =>
    {
        const p = M._tw_process() >> 2;
        const planar = M.HEAPF32.subarray(p, p + frame);
        const buf = new Float32Array(frame);

        for (let i = 0; i < window; i++)
            for (let c = 0; c < channels; c++)
                buf[i * channels + c] = planar[c * window + i];

        let peak = 0;

        for (let i = 0; i < frame; i++)
        {
            const a = Math.abs(buf[i]);

            if (a > peak)
                peak = a;
        }

        windows.push(buf);

        return peak;
    };

    M._tw_start();

    while (M._tw_now() < seconds)
    {
        M._tw_step(dt);
        drain();
        renderWindow();
    }

    /* stop() flushes the note-offs for whatever is still sounding; the
       releases that follow are part of the piece. */
    M._tw_stop();
    drain();

    for (let tail = 0; tail < TAIL_MAX; tail += dt)
        if (renderWindow() < TAIL_SILENT)
            break;

    if (tape !== null)
    {
        if (tapeText !== '')
            fs.writeSync(tape, tapeText);

        if (tape !== 1)
            fs.closeSync(tape);
    }

    let peak = 0;
    let sumsq = 0;
    let clipped = 0;

    for (const w of windows)
    {
        for (const v of w)
        {
            const a = Math.abs(v);

            if (a > peak)
                peak = a;

            if (a >= CLIP_LEVEL)
                clipped++;

            sumsq += v * v;
        }
    }

    const samples = windows.length * frame;

    if (wavFile !== '')
    {
        try
        {
            fs.writeFileSync(wavFile,
                             wavBytes(windows, samples, channels, rate));
        }
        catch
        {
            process.stderr.write(`${argv0}: cannot write ${wavFile}\n`);
            return 1;
        }
    }

    if (!quiet)
        process.stderr.write(
            `${genFile}: ${fixed(samples / frame * dt, 1)} s rendered, ` +
            `${notes} notes, peak ${fixed(peak, 3)}, ` +
            `RMS ${fixed(samples === 0 ? 0 : Math.sqrt(sumsq / samples), 4)}, ` +
            `${clipped} clipped sample${clipped === 1 ? '' : 's'}\n`);

    return clipped > 0 ? 3 : 0;
}

process.exitCode = await main(
    path.relative(process.cwd(), process.argv[1]) || process.argv[1],
    process.argv.slice(2));
