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
 * mirrortest.mjs -- the module twice in one process: one rendering, one
 * mirroring, both fed the same messages. One tape?
 *
 *   node wasm/web/mirrortest.mjs [BUILD_DIR]
 *
 * The first half of the composer-view gate. The composer view needs real
 * composer instances to draw, and the ones that sound are in the worklet, on
 * the audio thread, in another realm. So the mirror: a second instance of the
 * module, fed the messages host.js posts to the worklet, with a synth that
 * never renders (thSynth::setSilent) and tw_step in place of tw_render. If it
 * composes what the worklet composes, its pictures are the pictures of the
 * piece that is sounding.
 *
 * What makes that plausible is that neither side has a message handler of
 * its own: engine.js is the one switch, and both instances are driven
 * through it here exactly as the worklet and the worker are driven through
 * it in a browser. What makes it checked is the tape -- every note the
 * scheduler delivered, with its time -- taken from both and compared event
 * for event.
 *
 * The two are stepped as they are in a page, which is the part worth
 * getting right: the renderer in 128-frame quanta, the mirror told after
 * every sixteenth of them how far the renderer has got, exactly as the
 * tape batch will tell it. So the mirror steps in whole windows, a batch
 * behind, and has to arrive at the same tape all the same -- if it did
 * not, a command that landed in one window here and another there would be
 * the reason, and that is precisely the bug this is looking for.
 *
 * Then the two things a tape cannot show: that the mirror dropped no
 * commands (its ring is drained by the steps it is given, and a ring
 * nobody drains is what the silent synth exists to avoid), and that it
 * made no sound.
 *
 * Exit status is the number of failures.
 */

import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

import { drain, tapeLine } from '../tape.mjs';
import { apply } from './engine.js';
import { instruments, pieces } from './piececheck.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const build = path.resolve(process.argv[2] ??
                           path.join(here, '..', '..', 'build-web'));

const RATE = 48000;
const WINDOW = 256;
const BLOCK = 128;

/* worklet.js's TAPE_EVERY: how many quanta between posts to the page, and
   so how far behind the worklet the mirror is told to step. */
const TAPE_EVERY = 16;

const SECONDS = 20;

let failures = 0;

const fail = (what) =>
{
    process.stdout.write(`FAIL  ${what}\n`);
    failures++;
};

const { default: createThinkWeb } =
    await import(pathToFileURL(path.join(build, 'thinkweb.js')).href);

/* One instance, rendering or mirroring. `silent' is the whole difference:
   tw_silent before any load, and tw_step instead of tw_render after. */
async function instance ({ silent })
{
    const log = [];
    const M = await createThinkWeb({
        print: (s) => log.push(s),
        printErr: (s) => log.push(s),
    });

    M._tw_create(RATE, WINDOW, BLOCK);

    if (silent)
        M._tw_silent();

    return { M, log, tape: '', events: [] };
}

/* A message to both, the way host.js posts one to two ports. */
function post (peers, m)
{
    for (const p of peers)
        if (!apply(p.M, m, { log: (t) => p.log.push(t) }))
            throw new Error(`no message type '${m.type}'`);
}

/* The tape each instance has delivered since it was last asked. Written
   the way genwav writes it, which is the spelling every other harness
   here compares in. */
function takeTape (p)
{
    drain(p.M, p.events);

    for (const e of p.events.splice(0))
        p.tape += tapeLine(e);
}

/* The first stage whose picture is a control -- its module exports
   composer_input. Two do: `life', in colony and glider, and `ca', in
   loom and cavern. */
function clickable (M)
{
    for (let c = 0; c < M._tw_chain_count(); c++)
        for (let s = 0; s < M._tw_stage_count(c); s++)
            if (M._tw_stage_takes_input(c, s))
                return { chain: c, stage: s,
                         name: M.UTF8ToString(
                             M.ccall('tw_stage_name', 'number',
                                     ['number', 'number'], [c, s])) };

    return null;
}

/* A press, a drag and a release on a stage's picture: the gesture the
   canvas will send when somebody paints on an enlarged stage.
 *
   In the coordinates the picture was drawn in, which for this harness is
   a four-hundred-pixel square, and at transport times the run reaches.
   Where in the picture is the one thing that cannot be general: a plugin
   answers where it draws, and the two that take input draw different
   things. gen::life is a board filling the box, so the middle of it is a
   cell; gen::ca answers only in the band along the bottom, which is the
   present row -- the row a click edits -- and a click above it is
   correctly ignored. A click that landed outside what the plugin answers
   to would leave the tape unchanged, and the check at the end says so
   rather than passing quietly.
 *
   The three are far enough apart to land on three different cells in the
   narrowest board in the corpus, which is loom's sixteen: gen::ca toggles
   the cell under the press and under every drag, so a drag that came back
   over the cell the press landed on would toggle it off again and the
   gesture as a whole would do nothing at all. */
const DRAW = 400;

const PAINT_AT = { ca: 0.98, life: 0.5 };

function clicks (name)
{
    const y = (PAINT_AT[name] ?? 0.5) * DRAW;

    return [
        { at: 5.0, kind: 0, x: 0.25 * DRAW, y },
        { at: 5.1, kind: 1, x: 0.35 * DRAW, y },
        { at: 5.2, kind: 2, x: 0.45 * DRAW, y },
    ];
}

/* The same three gestures, but through the canvas rather than made up.
 *
 * This is the composer tab's own path with the DOM taken out: the canvas
 * in the mirror is shown the piece, a stage is enlarged, and a press, a
 * drag and a release arrive in shell pixels -- as a pointer would deliver
 * them. What the canvas does with them is the desktop's code: find the
 * enlarged stage, check the point is inside its picture, convert to the
 * coordinates the draw was handed. What it does NOT do here is call the
 * plugin: sigInput is connected, so each gesture comes back out as a
 * record for the shell to stamp and send.
 *
 * So the coordinates every peer will apply are the ones the code that
 * drew the rectangle worked out, which is the property that makes a
 * click land on the cell the clicker saw.
 */
function throughCanvas (M, control, view)
{
    M._tw_canvas_viewport(0, 0, view, view);
    M._tw_canvas_zoom_to_fit();
    M._tw_canvas_enlarge(control.chain, control.stage);

    /* The picture's rectangle, in the content's coordinates, from the
       canvas that laid it out; the shell's pixels are those times the
       zoom. */
    const zoom = M._tw_canvas_zoom();
    const x = M._tw_canvas_enlarged_x() * zoom;
    const y = M._tw_canvas_enlarged_y() * zoom;
    const w = M._tw_canvas_enlarged_w() * zoom;
    const h = M._tw_canvas_enlarged_h() * zoom;

    if (w <= 0 || h <= 0)
        return { sent: [], why: 'nothing was enlarged' };

    /* Where PAINT_AT says this composer answers, in shell pixels now. */
    const at = (fx) => [x + fx * w, y + (PAINT_AT[control.name] ?? 0.5) * h];

    M._tw_canvas_inputs_clear();

    M._tw_canvas_press(...at(0.25), 1, 1);
    M._tw_canvas_motion(...at(0.35));
    M._tw_canvas_release(...at(0.45), 1);

    const sent = [];

    for (let k = 0; k < M._tw_canvas_input_count(); k++)
        sent.push({
            chain: M._tw_canvas_input_chain(k),
            stage: M._tw_canvas_input_stage(k),
            kind: M._tw_canvas_input_kind(k),
            button: M._tw_canvas_input_button(k),
            x: M._tw_canvas_input_x(k),
            y: M._tw_canvas_input_y(k),
            w: M._tw_canvas_input_w(k),
            h: M._tw_canvas_input_h(k),
        });

    M._tw_canvas_inputs_clear();

    /* And the arithmetic, checked where it can be: a press a quarter of
       the way across the picture comes out a quarter of the way across
       the coordinates the plugin will be handed, whatever the zoom and
       wherever the picture sits in the view. */
    const why = sent.length !== 3
        ? `${sent.length} gestures came out of three`
        : Math.abs(sent[0].x / sent[0].w - 0.25) > 0.01 ||
          Math.abs(sent[1].x / sent[1].w - 0.35) > 0.01
            ? `a press at a quarter across came out at ` +
              `${(sent[0].x / sent[0].w).toFixed(3)}`
            : null;

    return { sent, why };
}

/* ---- one piece, both ways ---------------------------------------------- */

async function run (piece, dsps, { clicking = false, canvas = false } = {})
{
    const rendering = await instance({ silent: false });
    const mirror = await instance({ silent: true });
    const both = [rendering, mirror];

    for (const [name, text] of Object.entries(dsps))
        post(both, { type: 'instrument', name, text });

    let loaded = true;

    post(both, { type: 'piece', text: piece.text, seed: -1 });

    /* Both are asked to load the same piece and both have to agree that
       they did: a mirror that failed a load nobody looked at would
       compose an empty tape and match nothing. */
    for (const p of both)
        if (p.M._tw_piece_name() === 0)
            loaded = false;

    if (!loaded)
    {
        fail(`${piece.name}: did not load\n      ` +
             rendering.log.concat(mirror.log).join('\n      '));
        return null;
    }

    const control = clickable(rendering.M);

    /* The frames the module counts from are the page's, and the page's
       start wherever the audio context happens to be. One align on each
       puts them on one numbering, which is what lets the mirror be told a
       frame at all (thinkweb.cpp, tw_align). */
    const origin = 123456;

    post(both, { type: 'transport', op: 'start', frame: -1 });

    for (const p of both)
        p.M._tw_align(origin);

    /* A script with something in it for every path a command takes: a
       knob at a transport time, a tempo change, and a stop -- each stamped
       ahead of itself, which is what a peer's command is. */
    /* The piece's knob panel, which is where what a knob is now lives
       (src/KnobPanel.cpp). A row is one anybody can move, so a piece whose
       knobs are all hidden has no knob to move here either. */
    const first = rendering.M._tw_panel_open(1 /* thPanel::KNOB */, 0, 0)
        ? JSON.parse(
              rendering.M.UTF8ToString(rendering.M._tw_panel_json())).rows[0]
        : undefined;

    const knobs = first !== undefined;

    /* A knob is moved inside its own range and not to some number
       between nought and one: a piece's knob is in the piece's units, and
       loom's is a Wolfram rule between 0 and 255 whose value 0 empties
       the ring for good. Half its range is a knob moved; a hard zero is a
       piece switched off, and a piece switched off composes nothing for
       anything after it to disagree about. */
    const of = (f) => knobs ? first.lo + (first.hi - first.lo) * f : 0;

    /* The number the command names it by, which is the row's id and is
       not always 0: a piece whose first knob is hidden has no row for it
       and the first movable one is further along. */
    const which = knobs ? Number(first.id) : 0;

    const script = [
        { type: 'knob', at: 2.0, knob: which, value: of(0.6) },
        { type: 'at', op: 'tempo', at: 4.0, value: 150 },
        { type: 'knob', at: 6.0, knob: which, value: of(0.4) },
        { type: 'at', op: 'stop', at: 18.0 },
    ];

    for (const m of script)
        if (m.type !== 'knob' || knobs)
            post(both, m);

    /* And, where the piece takes them, keys. A piece that is nothing but
       live input -- gen/hands.gen -- composes not one event without them,
       and the `input midi' path is one a knob does not reach: the event
       goes into the chain rather than onto a channel, and a mirror that
       dropped it would be silent about a piece somebody is playing. */
    for (let c = 0; c < 16; c++)
    {
        if (!rendering.M._tw_listens(c))
            continue;

        for (const [k, note] of [60, 64, 67].entries())
        {
            post(both, { type: 'midion', frame: origin + (1 + k) * RATE,
                         channel: c, note, velocity: 100 });
            post(both, { type: 'midioff', frame: origin + (4 + k) * RATE,
                         channel: c, note });
        }

        break;
    }

    /* And the clicks, if this run is the clicked one: one more stamped
       command, applied at `at' inside the step on both instances -- the
       one that sounds and the one that will be drawn. */
    let gestures = [];

    if (clicking && control !== null && !canvas)
        gestures = clicks(control.name).map((c) => (
            { at: c.at, chain: control.chain, stage: control.stage,
              kind: c.kind, x: c.x, y: c.y, w: DRAW, h: DRAW, button: 1 }));

    /* Or the same three through the canvas, which is where a page's come
       from. The mirror's canvas, because that is the one a page draws. */
    if (clicking && control !== null && canvas)
    {
        if (!mirror.M._tw_canvas_show())
        {
            fail(`${piece.name}: the canvas could not read the piece`);
            return null;
        }

        const { sent, why } = throughCanvas(mirror.M, control, DRAW);

        if (why !== null)
        {
            fail(`${piece.name}: through the canvas, ${why}`);
            return null;
        }

        gestures = sent.map((g, i) => ({ ...g, at: 5.0 + i * 0.1 }));
    }

    for (const g of gestures)
        post(both, { type: 'input', ...g });

    /* And now the loop the page runs: the worklet renders a quantum at a
       time and posts a tape batch every sixteenth, and the mirror is
       stepped to the frame that batch reached. */
    let quanta = 0;

    /* Frames and not transport time: the script stops the transport before
       the end, and a loop that waited for a stopped transport to reach
       twenty seconds would wait for ever. */
    const until = origin + SECONDS * RATE;

    while (rendering.M._tw_frame() < until)
    {
        rendering.M._tw_render(BLOCK);
        takeTape(rendering);

        if (++quanta < TAPE_EVERY)
            continue;

        quanta = 0;
        mirror.M._tw_step(rendering.M._tw_frame());
        takeTape(mirror);
    }

    /* The batch the page would post last. The mirror is a batch behind all
       the way through and catches up here, which is the freshness the
       picture costs and nothing else. */
    mirror.M._tw_step(rendering.M._tw_frame());
    takeTape(mirror);

    const heard = rendering.tape.split('\n');
    const drawn = mirror.tape.split('\n');

    if (rendering.tape !== mirror.tape)
    {
        const at = heard.findIndex((line, i) => line !== drawn[i]);

        fail(`${piece.name}: the mirror composed something else\n` +
             `      at event ${at} of ${heard.length}\n` +
             `      sounding: ${heard[at] ?? '(nothing)'}\n` +
             `      mirror:   ${drawn[at] ?? '(nothing)'}`);
        return null;
    }

    if (rendering.tape.length === 0)
    {
        fail(`${piece.name}: composed nothing, so nothing was compared`);
        return null;
    }

    /* The reason the silent synth exists: a scheduler stepped over a synth
       nobody drains fills the command ring inside one fast-forward. */
    if (mirror.M._tw_dropped() !== 0)
    {
        fail(`${piece.name}: the mirror dropped ` +
             `${mirror.M._tw_dropped()} commands`);
        return null;
    }

    /* And it kept its word about the sound: tw_step renders nothing, so
       the block the renderer hands back is the mirror's silence. */
    const at = mirror.M._tw_render(BLOCK) >> 2;
    const out = mirror.M.HEAPF32.subarray(at, at + BLOCK * 2);

    if (out.some((v) => v !== 0))
    {
        fail(`${piece.name}: the mirror made a sound`);
        return null;
    }

    process.stdout.write(
        `ok    ${piece.name.padEnd(14)} ${heard.length - 1} events, one ` +
        `tape${gestures.length > 0
                   ? `, ${gestures.length} clicks on ${control.name}` +
                     `${canvas ? ' through the canvas' : ''}`
                   : ''}` +
        '\n');

    return { tape: rendering.tape, control };
}

/* ---- every seeded piece ------------------------------------------------- */

const dsps = instruments(build);

for (const piece of pieces(build))
{
    if (!piece.seeded)
    {
        process.stdout.write(`skip  ${piece.name.padEnd(14)} pins no seed; ` +
                             'it is not meant to repeat\n');
        continue;
    }

    const plain = await run(piece, dsps);

    if (plain === null || plain.control === null)
        continue;

    /* And the same piece with three clicks on the stage whose picture
       is a control. Two things have to be true of it: the mirror
       still composed what the renderer composed -- an input is a
       command and lands at the same point in the piece on both -- and
       the tape is *not* the unclicked one. A click that changed
       nothing would look exactly like agreement, which is the lesson
       gen/hands.gen taught. */
    const clicked = await run(piece, dsps, { clicking: true });

    if (clicked !== null && clicked.tape === plain.tape)
        fail(`${piece.name}: three clicks on ${plain.control.name} ` +
             'changed nothing, so nothing about them was tested');

    /* And once more with the gestures coming out of the canvas rather
       than out of this file: the press, the drag and the release arrive
       in shell pixels, the canvas works out which stage they are on and
       where in its picture, and what it hands back is what the page
       stamps and sends. The same tape on both instances again, and again
       not the untouched one. */
    const drawn = await run(piece, dsps, { clicking: true, canvas: true });

    if (drawn !== null && drawn.tape === plain.tape)
        fail(`${piece.name}: gestures through the canvas changed nothing`);
}

process.stdout.write(failures === 0
    ? '\na mirror fed the worklet\'s messages composes the worklet\'s ' +
      'piece\n'
    : `\n${failures} failed\n`);

process.exitCode = failures;
