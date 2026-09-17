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
 * drawcheck.mjs -- every picture in every shipped piece, drawn through the
 * module and read back as a list.
 *
 *   node wasm/web/drawcheck.mjs [BUILD_DIR]
 *
 * The first half of JAM_M6.md's section 3 gate. Eight composers draw their
 * state -- a Life board, a CA's grid, a Euclid ring -- and until now the
 * browser build left those draws out: there was no canvas in a worklet and
 * no cairo to link. wasm/cairo2d is the cairo they link now, and it records
 * rather than rasterises, so what a draw produces here is a list of ops
 * that wasm/cairo2d/replay.js replays on a Canvas2D.
 *
 * Each piece is played for a few seconds first, because what these draw is
 * their state and a piece that has not run has none: a markov with an empty
 * table draws an empty box on the desktop too. Then, for every stage that
 * has a picture, at 100x100 and at 400x400:
 *
 *   - The list is well formed: every op is one the table knows, and the
 *     arity table walks it to exactly its end. A list that does not is one
 *     the page would replay as operands read as opcodes.
 *   - It replays. Every op reaches replay.js's switch and comes out as
 *     Canvas2D calls, with nothing left for the default case -- which is
 *     the C and the JavaScript agreeing about the list a second time, over
 *     the real corpus rather than the stand-in's own fixture.
 *
 * Then the canvas around them, which is the same three questions of the
 * whole composer view: the desktop's ComposerCanvas compiled into the
 * module, laying the piece out and drawing it -- stage pictures and all,
 * since a stage's draw happens inside the canvas's own list.
 *
 * And over the corpus as a whole: every composer that says it draws has
 * drawn something somewhere. A composer whose draw compiled to nothing --
 * which is what a #ifdef left in the wrong place looks like -- passes every
 * per-picture check above and fails this one.
 *
 * The size matters because a draw takes its geometry from the w and h it
 * was handed; a composer that cached the first is one whose picture is
 * wrong in the enlarged view, and the two sizes here are the ones the
 * canvas actually uses -- a stage's box, and an enlarged stage.
 *
 * What this cannot see is whether the picture is right. Nothing headless
 * can; JAM_M6.md section 8.4 is two browsers and a person. What it can see
 * is everything between the composer and the canvas.
 *
 * Exit status is the number of failures.
 */

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

import { ARITY, OP_NAMES, replay } from '../cairo2d/replay.js';
import { instruments, pieces } from './piececheck.mjs';
import { loadPiece } from './render.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const build = path.resolve(process.argv[2] ??
                           path.join(here, '..', '..', 'build-web'));

/* A stage's box on the canvas, and a stage enlarged to fill the view. */
const SIZES = [100, 400];

/* And the canvas itself, at a window and at a panel. */
const VIEWS = [[900, 600], [400, 300]];

/* How much of each piece is played before its pictures are asked for. Long
   enough that a composer which learns from what passes through it has
   something to show, short enough that the corpus is seconds. */
const SECONDS = 4;
const BLOCK = 128;

let failures = 0;

const fail = (what) =>
{
    process.stdout.write(`FAIL  ${what}\n`);
    failures++;
};

/* ---- the list, out of the module's heap ------------------------------- */

/* The three tables cairo2d keeps, as JavaScript can use them. The ops are
   a view into HEAPF32 rather than a copy: it is what the page does, and a
   copy here would hide the one mistake that matters -- a length in bytes
   where a length in floats was meant. */
function readList (M)
{
    const at = M._tw_draw_ops();
    const words = M._tw_draw_words();
    const ops = words > 0
        ? M.HEAPF32.subarray(at >> 2, (at >> 2) + words)
        : new Float32Array(0);

    const strings = [];

    for (let i = 0; i < M._tw_draw_string_count(); i++)
        strings.push(M.UTF8ToString(M._tw_draw_string(i)));

    const surfaces = [];

    for (let i = 0; i < M._tw_draw_surface_count(); i++)
        surfaces.push({
            index: i,
            width: M._tw_draw_surface_width(i),
            height: M._tw_draw_surface_height(i),
            stride: M._tw_draw_surface_stride(i),
            data: M.HEAPU8.subarray(
                M._tw_draw_surface_data(i),
                M._tw_draw_surface_data(i) +
                    M._tw_draw_surface_stride(i) *
                    M._tw_draw_surface_height(i)),
        });

    return { ops, strings, surfaces };
}

/* Walk it with nothing but the arity table, which is the claim the
   encoding makes. Returns the ops it counted, or a sentence. */
function walk (ops)
{
    let count = 0;

    for (let i = 0; i < ops.length; )
    {
        const op = ops[i++];

        if (OP_NAMES[op] === undefined)
            return `op ${op} at word ${i - 1} is not one`;

        let arity = ARITY[op];

        if (arity < 0)                  /* SET_DASH says its own length */
        {
            if (i >= ops.length)
                return `${OP_NAMES[op]} at word ${i - 1} has no count`;

            arity = ops[i] + 2;
        }

        if (i + arity > ops.length)
            return `${OP_NAMES[op]} at word ${i - 1} runs off the end`;

        i += arity;
        count++;
    }

    return count;
}

/* A context that draws nothing and counts what it was asked for. The page
   hands replay.js a real one; what is under test here is that every op in
   the list is one replay.js knows, and it says so by throwing. */
function counter ()
{
    const nothing = () => { ctx.calls++; };
    const ctx = {
        calls: 0,
        beginPath: nothing, moveTo: nothing, lineTo: nothing,
        bezierCurveTo: nothing, arc: nothing, rect: nothing,
        closePath: nothing, fill: nothing, stroke: nothing, clip: nothing,
        fillRect: nothing, clearRect: nothing, fillText: nothing,
        drawImage: nothing, save: nothing, restore: nothing,
        translate: nothing, scale: nothing, setTransform: nothing,
        setLineDash: nothing,
    };

    return ctx;
}

/* ---- every piece, every stage, both sizes ------------------------------ */

const { default: createThinkWeb } =
    await import(pathToFileURL(path.join(build, 'thinkweb.js')).href);

const dsps = instruments(build);
const all = pieces(build);

let drawn = 0, canvases = 0, silentStages = 0;

/* Which composers drew, and which only said they would. */
const everDrew = new Map();

for (const piece of all)
{
    const { M, ok, log } = await loadPiece(createThinkWeb,
                                           { gen: piece.text,
                                             instruments: dsps });

    if (!ok)
    {
        fail(`${piece.name}: did not load\n      ${log.join('\n      ')}`);
        continue;
    }

    /* Played as the worklet plays it, and the tape thrown away: what is
       wanted is the state the stages end up in. */
    M._tw_transport(-1, 0, 0);

    while (M._tw_now() < SECONDS)
    {
        M._tw_render(BLOCK);
        M._tw_events_clear();
    }

    const pictures = [];

    for (let c = 0; c < M._tw_chain_count(); c++)
        for (let s = 0; s < M._tw_stage_count(c); s++)
        {
            const name = M.UTF8ToString(M.ccall('tw_stage_name', 'number',
                                                ['number', 'number'], [c, s]));

            if (!M._tw_stage_draws(c, s))
            {
                silentStages++;
                continue;
            }

            if (!everDrew.has(name))
                everDrew.set(name, false);

            for (const size of SIZES)
            {
                const words = M._tw_stage_draw(c, s, size, size);
                const where = `${piece.name}: ${name} at ` +
                              `${c}.${s}, ${size}x${size}`;

                if (words < 0)
                {
                    fail(`${where}: says it draws and would not`);
                    continue;
                }

                /* An empty picture is a picture: a composer draws its
                   state, and a stage whose state is still empty has
                   nothing to say. What is not allowed is a composer that
                   is empty everywhere, which is checked at the end. */
                if (words === 0)
                    continue;

                everDrew.set(name, true);

                const { ops, strings, surfaces } = readList(M);
                const walked = walk(ops);

                if (typeof walked === 'string')
                {
                    fail(`${where}: ${walked}`);
                    continue;
                }

                try
                {
                    replay(counter(), ops, strings, surfaces,
                           { width: size, height: size, dpr: 1 });
                }
                catch (e)
                {
                    fail(`${where}: ${e.message}`);
                    continue;
                }

                drawn++;
                pictures.push(`${name} ${walked}`);
            }
        }

    /* And the canvas around them: the desktop's ComposerCanvas, compiled
       into this module and drawing the same piece through the same cairo
       (JAM_M6.md, section 6). One list for the whole view, with each
       stage's picture inside it, drawn by the plugin through the context
       the canvas handed it. */
    if (!M._tw_canvas_show())
        fail(`${piece.name}: the composer canvas could not read the piece`);
    else
        for (const [w, h] of VIEWS)
        {
            M._tw_canvas_viewport(0, 0, w, h);
            M._tw_canvas_zoom_to_fit();

            const words = M._tw_canvas_draw(w, h);
            const where = `${piece.name}: the canvas at ${w}x${h}`;

            if (words <= 0)
            {
                fail(`${where}: drew nothing`);
                continue;
            }

            const { ops, strings, surfaces } = readList(M);
            const walked = walk(ops);

            if (typeof walked === 'string')
            {
                fail(`${where}: ${walked}`);
                continue;
            }

            try
            {
                replay(counter(), ops, strings, surfaces,
                       { width: w, height: h, dpr: 1 });
            }
            catch (e)
            {
                fail(`${where}: ${e.message}`);
                continue;
            }

            canvases++;
            pictures.push(`canvas ${walked}`);
        }

    if (pictures.length === 0)
        process.stdout.write(`ok    ${piece.name.padEnd(14)} nothing in it ` +
                             'draws\n');
    else
        process.stdout.write(`ok    ${piece.name.padEnd(14)} ` +
                             `${pictures.join(', ')}\n`);
}

/* Eight composers export a draw. One that never produced a list anywhere in
   the corpus is either a draw that does nothing or a piece list that never
   reaches it, and both are worth a sentence. */
for (const [name, drew] of everDrew)
    if (!drew)
        fail(`${name} draws, and drew nothing anywhere in the corpus`);

process.stdout.write(failures === 0
    ? `\n${drawn} pictures drawn and replayed, from ${everDrew.size} ` +
      `composers, and ${canvases} whole canvases; ${silentStages} stages ` +
      'have no picture\n'
    : `\n${failures} failed\n`);

process.exitCode = failures;
