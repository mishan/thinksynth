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
 * mirror.js -- the same module again, in a worker, composing what the
 * worklet composes and drawing it.
 *
 * The composer view shows real composer instances -- a Life board, a CA's
 * grid -- and the instances that are sounding are in the worklet, on the
 * audio thread, in a realm the page cannot reach into. So there is a
 * second set of them here, fed the messages the worklet is fed (host.js
 * posts each to both ports) and stepped to the frame the worklet's last
 * tape batch reached. Its synth never renders: notes stop at the door and
 * process() applies the queue and skips the DSP (thSynth::setSilent).
 *
 * A worker rather than the main thread, for three reasons in order: the
 * mirror's job is to receive the same messages the worklet receives, and a
 * worker with the worklet's own handler gets that by construction; a
 * fast-forward runs for seconds and must not freeze the page; and docs/JAM.md's
 * description of the mirror -- another peer that renders nothing -- is then
 * literally what it is.
 *
 * What it sends back: its own tape, for the page to hold against the
 * worklet's, and display lists -- the composer canvas and the piano roll
 * both live here, next to the scheduler they draw from, and what crosses
 * is the list of ops the page replays on a Canvas2D (cairo-canvas2d). The
 * roll is here for a reason of its own on top of that one: half of what it
 * draws is the scheduler's *pending* queue, and a tape -- which is what the
 * page used to draw a roll from -- is by definition what has already been
 * delivered.
 *
 * A gesture goes the other way and comes back round: the page sends the
 * pointer, the canvas here works out which stage it landed on and where in
 * its picture, and what comes back is a record the page stamps and sends
 * as a command -- to the worklet, to the peers, and to this instance,
 * which hears its own click at its time like everyone else (section 5).
 */

import createThinkWeb from './thinkweb.js';
import { apply } from './engine.js';
import { drain } from './tape.js';

/* thinkweb.cpp's CanvasContent::Key. */
const KEYS = { Escape: 1 };

let M = null;
const early = [];

let events = [];
let epoch = 0;

/* The last size the page asked for a drawing at, in CSS pixels, and the
   device pixel ratio it will replay at. The canvas lays out in CSS pixels
   and the replayer scales; the ratio is here only so that a list drawn for
   one size is not replayed at another.

   One per canvas: the composer view and the piano roll are two panes and
   two boxes, and a single `view' had whichever of them drew last decide
   how big the other one was. */
let view = { w: 0, h: 0, dpr: 1 };
let rollView = { w: 0, h: 0, dpr: 1 };

const post = (m, transfer) => postMessage(m, transfer ?? []);
const log = (text) => post({ type: 'log', text: `mirror: ${text}` });

async function start ({ bytes, windowlen, sampleRate })
{
    M = await createThinkWeb({
        instantiateWasm: (imports, done) =>
        {
            WebAssembly.instantiate(bytes, imports)
                .then((r) => done(r.instance, r.module))
                .catch((e) => log(`the wasm did not compile: ${e}`));

            return {};
        },
        print: log,
        printErr: log,
    });

    M._tw_create(sampleRate, windowlen, 128);

    /* Before anything is loaded: it is a kind of synth and not a mode a
       running one flips (thSynth::setSilent). */
    M._tw_silent();

    epoch = M._tw_epoch();
    post({ type: 'ready' });

    for (const m of early.splice(0))
        receive(m);
}

/* The piece the canvas is now showing, in the little the page needs to
 * know about it: the chains by name, and their stages with whether each
 * has a picture and whether that picture is a control. Sent once per load.
 *
 * The page uses it to offer the stages that can be painted on; everything
 * else about the drawing it never needs to know, because it does not do
 * the drawing.
 */
function showPiece ()
{
    if (!M._tw_canvas_show())
    {
        post({ type: 'piece', chains: [] });
        return;
    }

    /* A piece that has just loaded is a drawing that did not exist when
       the page last asked for a fit -- and a fit of nothing is dropped,
       since there is nothing to fit to. So the fit happens here, where
       there is. */
    M._tw_canvas_zoom_to_width();

    const chains = [];

    for (let c = 0; c < M._tw_chain_count(); c++)
    {
        const stages = [];

        for (let s = 0; s < M._tw_stage_count(c); s++)
            stages.push({
                stage: s,
                name: M.UTF8ToString(M.ccall('tw_stage_name', 'number',
                                             ['number', 'number'], [c, s])),
                draws: M._tw_stage_draws(c, s) !== 0,
                takesInput: M._tw_stage_takes_input(c, s) !== 0,
            });

        chains.push({
            chain: c,
            name: M.UTF8ToString(M.ccall('tw_chain_name', 'number',
                                         ['number'], [c])),
            /* Where this chain is heard, for a view that draws a track
               and has to say what plays it. -1 for a chain that only
               writes knobs. */
            channel: M._tw_chain_channel(c),
            stages,
        });
    }

    post({ type: 'piece', chains });
}

/* Everything the canvas has decided to send since it was last asked. Each
   becomes an `input' command on the page. */
function sendGestures ()
{
    for (let k = 0; k < M._tw_canvas_input_count(); k++)
        post({
            type: 'input',
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

    /* And the params popover, if a stage's handle was clicked. Read here
       rather than asked for later, because the page has no scheduler to
       read them from: this instance is where the piece is. */
    if (!M._tw_canvas_params_wanted())
        return;

    const chain = M._tw_canvas_params_chain();
    const stage = M._tw_canvas_params_stage();
    const params = [];

    for (let p = 0; p < M._tw_stage_param_count(chain, stage); p++)
        params.push({
            name: string('tw_stage_param_name', chain, stage, p),
            desc: string('tw_stage_param_desc', chain, stage, p),
            units: string('tw_stage_param_units', chain, stage, p),
            knob: string('tw_stage_param_knob', chain, stage, p),
            text: string('tw_stage_param_text', chain, stage, p),
            type: M._tw_stage_param_type(chain, stage, p),
            value: M._tw_stage_param_value(chain, stage, p),
            min: M._tw_stage_param_min(chain, stage, p),
            max: M._tw_stage_param_max(chain, stage, p),
        });

    post({
        type: 'params',
        chain,
        stage,
        name: M.UTF8ToString(M.ccall('tw_stage_name', 'number',
                                     ['number', 'number'], [chain, stage])),
        chainName: M.UTF8ToString(M.ccall('tw_chain_name', 'number',
                                          ['number'], [chain])),
        at: { x: M._tw_canvas_params_x(), y: M._tw_canvas_params_y(),
              w: M._tw_canvas_params_w(), h: M._tw_canvas_params_h() },
        params,
    });
}

/* One of the many `const char *' exports, as a string. */
function string (name, chain, stage, p)
{
    return M.UTF8ToString(
        M.ccall(name, 'number', ['number', 'number', 'number'],
                [chain, stage, p]));
}

/* The three tables a draw leaves behind -- the ops, the strings they
 * index, the surfaces they blit -- copied out of the heap.
 *
 * Copied rather than viewed, and the copies transferred: a view into
 * HEAPF32 is a view into memory this worker goes on writing to, and a
 * transfer costs nothing on top of the copy. `buffers' is what the post
 * hands to postMessage as its transfer list.
 *
 * Shared by the canvas and by one stage's picture, because they are the
 * same three tables (thinkweb.cpp): a stage's draw and the canvas's are
 * one recorder.
 */
function readList (words)
{
    const at = M._tw_draw_ops();
    const ops = words > 0
        ? new Float32Array(M.HEAPF32.subarray(at >> 2, (at >> 2) + words))
        : new Float32Array(0);

    const strings = [];

    for (let i = 0; i < M._tw_draw_string_count(); i++)
        strings.push(M.UTF8ToString(M._tw_draw_string(i)));

    const surfaces = [];
    const buffers = [ops.buffer];

    for (let i = 0; i < M._tw_draw_surface_count(); i++)
    {
        const from = M._tw_draw_surface_data(i);
        const stride = M._tw_draw_surface_stride(i);
        const height = M._tw_draw_surface_height(i);
        const data = new Uint8Array(
            M.HEAPU8.subarray(from, from + stride * height));

        surfaces.push({ index: i, width: M._tw_draw_surface_width(i),
                        height, stride, data });
        buffers.push(data.buffer);
    }

    return { ops, strings, surfaces, buffers };
}

/* One frame of the canvas: the list, the strings it indexes and the
 * surfaces it blits, out of the heap and over to the page.
 *
 * Copied rather than viewed, and the copies transferred: a view into
 * HEAPF32 is a view into memory this worker goes on writing to, and a
 * transfer costs nothing on top of the copy.
 */
function draw ()
{
    if (M === null || view.w <= 0 || view.h <= 0)
        return;

    /* At the drawing's own size, not the view's: the element is as big as
       the drawing and the scroller around it is what moves, which is what
       a Gtk scrolled window does to the widget on the desktop. Before a
       piece has loaded there is no drawing, and the view's size is as
       good an answer as any. */
    const w = M._tw_canvas_width() || view.w;
    const h = M._tw_canvas_height() || view.h;
    const words = M._tw_canvas_draw(w, h);

    if (words < 0)
        return;                 /* no canvas yet: no piece has loaded */

    const { ops, strings, surfaces, buffers } = readList(words);

    /* The drawing's own size, for the scroller around the element, and
       the size this list was drawn at, so a page that has resized since
       can tell that it is a frame behind. */
    post({ type: 'draw', ops, strings, surfaces,
           w, h, dpr: view.dpr,
           width: M._tw_canvas_width(), height: M._tw_canvas_height(),
           zoom: M._tw_canvas_zoom(),
           enlarged: { chain: M._tw_canvas_enlarged_chain(),
                       stage: M._tw_canvas_enlarged_stage(),
                       x: M._tw_canvas_enlarged_x(),
                       y: M._tw_canvas_enlarged_y(),
                       w: M._tw_canvas_enlarged_w(),
                       h: M._tw_canvas_enlarged_h() } },
         buffers);
}

/* ---- the piano roll ----
 *
 * The other canvas drawn here, and here for a reason the composer view
 * only half shares: what the roll shows to the right of the now-line is
 * the scheduler's *pending* queue -- what the piece has already decided
 * and not yet played -- and this worker holds the only scheduler the page
 * has. The worklet's tape cannot answer the question at all: a tape is
 * what has been delivered.
 *
 * The messages are the canvas view's, with `canvas: "roll"' on them, so
 * one shell file drives both panes and the routing is one line.
 */
function drawRoll ()
{
    if (M === null || rollView.w <= 0 || rollView.h <= 0)
        return;

    /* The roll's drawing is always exactly its view (src/RollCanvas.h),
       so there is no separate drawing size to ask for and nothing to
       scroll. */
    const w = M._tw_roll_width() || rollView.w;
    const h = M._tw_roll_height() || rollView.h;
    const words = M._tw_roll_draw(w, h);

    if (words < 0)
        return;                 /* no scheduler yet: nothing to draw */

    const at = M._tw_draw_ops();
    const ops = words > 0
        ? new Float32Array(M.HEAPF32.subarray(at >> 2, (at >> 2) + words))
        : new Float32Array(0);

    const strings = [];

    for (let i = 0; i < M._tw_draw_string_count(); i++)
        strings.push(M.UTF8ToString(M._tw_draw_string(i)));

    /* No surfaces: the roll blits nothing. The page's replayer takes the
       empty list and the message is that much smaller. */
    post({ type: 'draw', canvas: 'roll', ops, strings, surfaces: [],
           w, h, dpr: rollView.dpr, width: w, height: h,
           now: M._tw_roll_view_now(),
           following: M._tw_roll_following() !== 0,
           spanPast: M._tw_roll_span_past(),
           spanFuture: M._tw_roll_span_future() },
         [ops.buffer]);
}

/* True if the message was the roll's. */
function rollReceive (m)
{
    switch (m.type)
    {
        case 'view':
            rollView = { w: m.w, h: m.h, dpr: m.dpr ?? 1 };
            M._tw_roll_viewport(m.x ?? 0, m.y ?? 0, m.w, m.h);

            /* `fit' is dropped, and deliberately: the roll's drawing is
               its view, so every fit this base offers computes exactly 1
               (src/RollCanvas.h). */
            break;

        case 'press':
            M._tw_roll_press(m.x, m.y, m.button ?? 1, m.nPress ?? 1);
            break;

        case 'motion':
            M._tw_roll_motion(m.x, m.y);
            break;

        case 'release':
            M._tw_roll_release(m.x, m.y, m.button ?? 1);
            break;

        /* A wheel notch. The shell says how much bigger to draw and the
           roll spends it as a span, which is the one place a number here
           is read as something other than its name -- RollCanvas::zoomBy
           says why. */
        case 'zoomBy':
            M._tw_roll_zoom_by(m.by);
            break;

        case 'draw':
            drawRoll();
            break;
    }
}

function receive (m)
{
    if (m.type === 'start')
    {
        start(m).catch((e) => log(String(e)));
        return;
    }

    if (M === null)
    {
        early.push(m);
        return;
    }

    /* The messages the worklet gets, applied the same way (engine.js).
       The piece is the one with something else to do: the canvas has to
       be shown what loaded. */
    if (apply(M, m, { log, piece: (id, ok) => ok && showPiece() }))
        return;

    /* The piano roll's half of the canvas-view protocol, told apart by
       the page tagging it. Everything below is the composer view's. */
    if (m.canvas === 'roll')
    {
        rollReceive(m);
        return;
    }

    switch (m.type)
    {
        /* This instance's frames are the worklet's from here on
           (thinkweb.cpp, tw_align). */
        case 'align':
            M._tw_align(m.frame);
            break;

        /* As far as the worklet's last tape batch reached: the commands
           due, the transport, the queue drained, and the tape. */
        case 'step':
        {
            M._tw_step(m.frame);

            const now = M._tw_epoch();

            if (now !== epoch)
            {
                if (events.length > 0)
                    postTape();

                epoch = now;
            }

            drain(M, events);
            postTape();
            break;
        }

        /* The page's element resized, scrolled or zoomed. */
        case 'view':
            view = { w: m.w, h: m.h, dpr: m.dpr ?? 1 };
            M._tw_canvas_viewport(m.x ?? 0, m.y ?? 0, m.w, m.h);

            /* To the width, and the scroller takes the rest: a piece is
               a row per chain and a tall one fitted both ways is a
               quarter-scale picture nobody can read
               (CanvasContent::zoomToWidth). */
            if (m.fit)
                M._tw_canvas_zoom_to_width();

            break;

        case 'zoom':
            M._tw_canvas_set_zoom(m.zoom);
            break;

        /* Ctrl+wheel: a step in, a step out. The shell says by how much
           and this says from what, since the zoom is the content's. */
        case 'zoomBy':
            M._tw_canvas_set_zoom(M._tw_canvas_zoom() * m.by);
            break;

        case 'fit':
            M._tw_canvas_zoom_to_fit();
            break;

        case 'enlarge':
            M._tw_canvas_enlarge(m.chain ?? -1, m.stage ?? -1);
            break;

        /* Where a stage's params handle is, for a page that wants to
           press one without repeating the layout arithmetic. */
        case 'handle':
            post({ type: 'handle', chain: m.chain, stage: m.stage,
                   x: M._tw_canvas_handle_x(m.chain, m.stage),
                   y: M._tw_canvas_handle_y(m.chain, m.stage) });
            break;

        /* The pointer. The canvas decides what it landed on; whatever it
           wants sent goes back as a gesture to be stamped. */
        case 'press':
            M._tw_canvas_press(m.x, m.y, m.button ?? 1, m.nPress ?? 1);
            sendGestures();
            break;

        case 'motion':
            M._tw_canvas_motion(m.x, m.y);
            sendGestures();
            break;

        case 'release':
            M._tw_canvas_release(m.x, m.y, m.button ?? 1);
            sendGestures();
            break;

        case 'key':
            M._tw_canvas_key(KEYS[m.key] ?? 0);
            sendGestures();
            break;

        /* A frame, asked for by the page on its own animation frames and
           not at all while the view is hidden. Unconditional: what these
           draw is composer state, which moves whether or not anybody
           touched the canvas -- the desktop redraws on a 50 ms timer for
           the same reason. */
        case 'draw':
            draw();
            break;

        /* One stage's own picture, at the size the asker will replay it
         * at. The canvas's draw is a whole piece laid out as rows; this
         * is what a view wants that has already decided where a stage
         * goes -- a track in a sequencer is one grid and nothing else
         * around it, and laying the canvas out to get at one box of it
         * would be drawing the other twenty for nobody. */
        case 'stagedraw':
        {
            const words = M._tw_stage_draw(m.chain, m.stage, m.w, m.h);

            /* Every ask is answered, including "no such stage, or it
               does not draw" -- with no list, which is a different thing
               from an empty one and says to leave the picture alone. The
               asker counts what is out and not back so it does not queue
               frames on a worker already behind, and a request that got
               no reply at all stopped that count coming down for good:
               load a piece with fewer chains while the frames are in
               flight and the pane froze. */
            const list = words < 0 ? null : readList(words);

            post({ type: 'stagedraw', chain: m.chain, stage: m.stage,
                   w: m.w, h: m.h, dpr: m.dpr ?? 1,
                   ops: list?.ops ?? null, strings: list?.strings ?? null,
                   surfaces: list?.surfaces ?? null },
                 list?.buffers ?? []);
            break;
        }

        /* What a set of .patch texts say they are, read the once.
         *
         * A menu wants to offer the patches that are for the graph a
         * channel is holding, which is a question about every shipped
         * patch and no channel at all. It is asked here rather than of
         * the worklet because the worklet's thread is the one making
         * sound, and seventy-seven parses do not belong on it -- and
         * asked of the module rather than answered in the page, because
         * the format has one reading and it is in C++. */
        case 'patchinfo':
        {
            const read = [];

            for (const item of m.items ?? [])
            {
                const doc = JSON.parse(M.UTF8ToString(
                    M.ccall('tw_patch_reads', 'number', ['string'],
                            [item.text])));

                if (doc.dsp === undefined)
                    continue;

                read.push({ name: item.name, dsp: doc.dsp,
                            title: doc.info?.title });
            }

            post({ type: 'patchinfo', items: read });
            break;
        }

        /* Every param of one stage, by name and value. The page has no
           scheduler to read them from; this instance is where the piece
           is, which is the same reason the params popover is answered
           from here. */
        case 'stageparams':
        {
            const params = [];

            for (let p = 0;
                 p < M._tw_stage_param_count(m.chain, m.stage); p++)
                params.push({
                    name: string('tw_stage_param_name', m.chain, m.stage, p),
                    value: M._tw_stage_param_value(m.chain, m.stage, p),
                    text: string('tw_stage_param_text', m.chain, m.stage, p),
                });

            post({ type: 'stageparams', chain: m.chain, stage: m.stage,
                   params });
            break;
        }
    }
}

function postTape ()
{
    post({ type: 'tape', epoch, now: M._tw_now(), frame: M._tw_frame(),
           running: M._tw_running() !== 0, late: M._tw_late(),
           dropped: M._tw_dropped(), events });
    events = [];
}

onmessage = (e) => receive(e.data);
