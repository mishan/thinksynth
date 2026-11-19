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
 * nodecheck.mjs -- the node editor's model, through the browser module.
 *
 *   node wasm/web/nodecheck.mjs [BUILD_DIR] [NATIVE_BUILD_DIR]
 *
 * The .dsp canvas in a room is the desktop's NodeGraph, NodeEdit,
 * NodeLayout and NodeCatalog compiled to wasm and run over the document's
 * text. Two things have to be true of that, and they are different
 * things.
 *
 * The first is that the model still works when it is the browser running
 * it: every shipped .dsp builds a graph with boxes and ports in it, the
 * palette has the plugins this module was compiled with, and an edit comes
 * back as a patch that still parses and says what the edit asked for.
 *
 * The second is the one that matters in a room: the edit has to be the
 * edit the desktop would have made, byte for byte. A .dsp somebody changes
 * in a browser is a .dsp somebody else opens in the editor, and "nearly
 * the same" is a patch that quietly stops being the patch. So every edit
 * here is made twice -- once through the module, once by scripts/dspedit,
 * which is the native NodeEdit -- and the two texts are compared.
 *
 * `add-node' is the one edit not compared that way: the module fills in
 * the plugin's own declared defaults from the catalogue and dspedit has no
 * catalogue to ask. It is checked for what it produces instead -- a patch
 * that parses, with the node in it.
 *
 * Exit status is the number of failures.
 */

import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const top = path.join(here, '..', '..');
const build = path.resolve(process.argv[2] ?? path.join(top, 'build-web'));
const native = path.resolve(process.argv[3] ?? path.join(top, 'build'));

const dspedit = path.join(native, 'scripts', 'dspedit');

let failures = 0;

const fail = (what) =>
{
    process.stdout.write(`FAIL  ${what}\n`);
    failures++;
};

if (!fs.existsSync(dspedit))
{
    process.stdout.write(
        `nodecheck: no dspedit in ${native}; build the native tree first ` +
        '-- it is what the module is compared against.\n');
    process.exit(1);
}

const { default: createThinkWeb } =
    await import(pathToFileURL(path.join(build, 'thinkweb.js')).href);

const log = [];
const M = await createThinkWeb({
    print: (s) => log.push(s),
    printErr: (s) => log.push(s),
});

/* Two ways of calling in: a string in, a string out. */
const call = (name, types, args) =>
    M.UTF8ToString(M.ccall(name, 'number', types, args));

const edit = (name, types, args) =>
{
    const r = M.ccall(name, 'number', types, args);

    return { r, text: M.UTF8ToString(M._tw_edit_text()),
             why: M.UTF8ToString(M._tw_edit_why()) };
};

/* NodeEdit::Result. */
const OK = 0;

/* thPanel::Kind, for the panel over a box, and thPanelRow::Kind's number
   box. */
const NODE_VALUE = 3;
const NUMBER = 2;

/* ---- the palette ------------------------------------------------------- */

{
    const count = M._tw_catalog_take();
    const categories = [];

    for (let c = 0; c < M._tw_catalog_category_count(); c++)
        categories.push(call('tw_catalog_category', ['number'], [c]));

    /* The plugins compiled into this module, minus the composers, which
       are in the same table and are not nodes. */
    if (count > 0 && categories.length > 0)
        process.stdout.write(
            `ok    the palette has ${count} plugins in ` +
            `${categories.length} categories: ${categories.join(', ')}\n`);
    else
        fail(`the palette has ${count} plugins in ${categories.length} ` +
             'categories');

    /* And every one of them loads and says what its ports are, which is
       what the palette shows and what a wire needs. */
    let described = 0, ports = 0;

    for (const category of categories)
    {
        const many = M.ccall('tw_catalog_in_category', 'number', ['string'],
                             [category]);

        for (let i = 0; i < many; i++)
        {
            const spelling = call('tw_catalog_spelling',
                                  ['string', 'number'], [category, i]);

            if (!M.ccall('tw_catalog_describe', 'number', ['string'],
                         [spelling]))
            {
                fail(`${spelling} is in the palette and will not load`);
                continue;
            }

            described++;
            ports += M._tw_catalog_port_count();
        }
    }

    if (described === count)
        process.stdout.write(
            `ok    all ${described} of them load, with ${ports} ports ` +
            'between them\n');
    else
        fail(`${described} of ${count} plugins in the palette describe ` +
             'themselves');
}

/* ---- every shipped patch, as a graph ------------------------------------ */

const dspDir = path.join(build, 'dsp');

/* The index carries the kit as well, as `samples/<name>' -- the wavs
   osc::sample plays. The page hands the worklet every name in the list
   because a worklet cannot fetch, so they are in there with the patches;
   everything that treats the list as patches filters them out, and this
   reads every entry as text and builds a graph from it. `fx/' stays: an
   effect is a real .dsp with a real graph in it, which is the difference.
   Without this the wavs come back as `stray character' and seven patches
   that are not patches fail to build. */
const names = JSON.parse(fs.readFileSync(path.join(dspDir, 'index.json'),
                                         'utf8'))
    .filter((n) => !n.startsWith('samples/'));

let graphs = 0, boxes = 0, edits = 0;

for (const name of names)
{
    const file = path.join(dspDir, name);
    const text = fs.readFileSync(file, 'utf8');
    const built = M.ccall('tw_graph_build', 'number', ['string'], [text]);

    if (built <= 0)
    {
        fail(`${name}: built ${built} boxes\n      ` +
             log.splice(0).join('\n      '));
        continue;
    }

    log.length = 0;
    graphs++;
    boxes += built;

    /* The io node is two boxes -- the MIDI source and the audio sink --
       which is the thing about this graph that a second implementation
       would most likely get wrong. */
    let sources = 0, sinks = 0;

    for (let b = 0; b < built; b++)
    {
        const kind = M._tw_graph_box_kind(b);

        if (kind === 2)
            sources++;
        else if (kind === 3)
            sinks++;
    }

    if (sources !== 1 || sinks !== 1)
        fail(`${name}: ${sources} io sources and ${sinks} io sinks`);

    /* The saved positions in the file, applied over the computed layout. */
    M.ccall('tw_graph_apply_layout', 'number', ['string'], [text]);

    /* An edit of each kind the desktop makes, held against the desktop's
       own answer. The first plain value of the first node with one, and
       the first control, which every shipped patch has. */
    let where = null;

    for (let b = 0; b < built && where === null; b++)
    {
        /* A plain value: not wired, not an output, and a number. Which of a
           box's params those are is the panel's answer (src/NodePanel.cpp):
           an offered number box, the same rule the page draws one from. A
           Name or arithmetic is offered too, as text. */
        if (M._tw_graph_box_kind(b) !== 0 ||
            M._tw_graph_box_settable(b) === 0 ||
            M._tw_panel_open(NODE_VALUE, b, 0) === 0)
            continue;

        const row = JSON.parse(M.UTF8ToString(M._tw_panel_json()))
                        .rows.find((r) => r.editable && r.kind === NUMBER);

        if (row === undefined)
            continue;

        where = {
            node: call('tw_graph_box_name', ['number'], [b]),
            arg: row.id,
            was: row.value,
        };
    }

    if (where === null)
        continue;               /* nothing plain to set; rare but legal */

    const value = where.was + 0.125;
    const got = edit('tw_edit_set_value',
                     ['string', 'string', 'string', 'number'],
                     [text, where.node, where.arg, value]);

    if (got.r !== OK)
    {
        fail(`${name}: setting ${where.node}.${where.arg} answered ` +
             `${call('tw_edit_result_text', ['number'], [got.r])} (${got.why})`);
        continue;
    }

    const want = execFileSync(dspedit,
                              [file, 'set-value', where.node, where.arg,
                               String(value)],
                              { encoding: 'utf8' });

    if (got.text !== want)
    {
        fail(`${name}: the module's edit of ${where.node}.${where.arg} is ` +
             'not the one the desktop makes');
        continue;
    }

    /* And it is a patch: it parses, and the value took. */
    const again = M.ccall('tw_graph_build', 'number', ['string'],
                          [got.text]);

    if (again !== built)
    {
        fail(`${name}: the edited patch builds ${again} boxes, not ${built}`);
        continue;
    }

    edits++;
}

process.stdout.write(
    `ok    ${graphs} patches built ${boxes} boxes, and ${edits} edits are ` +
    'the edits the desktop makes\n');

/* ---- and the canvas over every one of them ------------------------------ */

/* The other half of the drawing gate: the node canvas, which is the
   desktop's, drawn over every shipped .dsp through the cairo stand-in. The
   list has to be walkable by the arity table alone and known to replay.js
   op for op -- the same three questions drawcheck asks of the composer
   canvas, of the other canvas. */
{
    const { ARITY, OP_NAMES, replay } =
        await import('cairo-canvas2d');

    const nothing = () => {};
    const ctx = new Proxy({}, {
        get: (target, name) => name in target ? target[name] : nothing,
        set: () => true,
    });

    let drawn = 0, ops = 0;

    for (const name of names)
    {
        const text = fs.readFileSync(path.join(dspDir, name), 'utf8');

        if (M.ccall('tw_graph_build', 'number', ['string'], [text]) <= 0)
            continue;

        M.ccall('tw_graph_apply_layout', 'number', ['string'], [text]);
        M._tw_node_canvas_viewport(0, 0, 900, 600);
        M._tw_node_canvas_zoom_to_fit();

        const words = M._tw_node_canvas_draw(M._tw_node_canvas_width() || 900,
                                             M._tw_node_canvas_height() || 600);

        if (words <= 0)
        {
            fail(`${name}: the node canvas drew nothing`);
            continue;
        }

        const at = M._tw_draw_ops();
        const list = M.HEAPF32.subarray(at >> 2, (at >> 2) + words);
        const strings = [];

        for (let i = 0; i < M._tw_draw_string_count(); i++)
            strings.push(M.UTF8ToString(M._tw_draw_string(i)));

        let bad = null;

        for (let i = 0; i < list.length && bad === null; )
        {
            const op = list[i++];

            if (OP_NAMES[op] === undefined)
                bad = `op ${op} at word ${i - 1} is not one`;
            else
            {
                const arity = ARITY[op] < 0 ? list[i] + 2 : ARITY[op];

                if (i + arity > list.length)
                    bad = `${OP_NAMES[op]} runs off the end`;

                i += arity;
            }
        }

        if (bad !== null)
        {
            fail(`${name}: the node canvas's list is malformed: ${bad}`);
            continue;
        }

        try
        {
            replay(ctx, list, strings, [],
                   { width: 900, height: 600, dpr: 1 });
        }
        catch (e)
        {
            fail(`${name}: the node canvas's list did not replay: ` +
                 `${e.message}`);
            continue;
        }

        drawn++;
        ops += words;
    }

    process.stdout.write(
        `ok    the node canvas drew ${drawn} patches, ${ops} words of ops, ` +
        'every one replayable\n');

    /* And it answers a pointer. The canvas is the only thing that knows
       what was clicked, and what it decides comes back as signals for the
       page to act on -- an edit, a rebuild, a line in the status bar,
       which is what NodeEditor does with the same ones on the desktop. */
    const text = fs.readFileSync(path.join(dspDir, 'ts1.dsp'), 'utf8');

    M.ccall('tw_graph_build', 'number', ['string'], [text]);
    M.ccall('tw_graph_apply_layout', 'number', ['string'], [text]);
    M._tw_node_canvas_viewport(0, 0, 900, 600);
    M._tw_node_canvas_set_zoom(1);
    M._tw_node_signals_clear();

    /* A press a little inside the first box, in shell pixels, which at a
       zoom of one are the graph's own. */
    M._tw_node_canvas_press(M._tw_graph_box_x(0) + 6,
                            M._tw_graph_box_y(0) + 6, 1, 1);
    M._tw_node_canvas_release(M._tw_graph_box_x(0) + 6,
                              M._tw_graph_box_y(0) + 6, 1);

    const said = [];

    for (let i = 0; i < M._tw_node_signal_count(); i++)
        said.push(M._tw_node_signal_kind(i));

    if (M._tw_node_canvas_selected() === 0 && said.includes(1))
        process.stdout.write(
            'ok    a press on a box selects it and says so\n');
    else
        fail(`a press on the first box selected ` +
             `${M._tw_node_canvas_selected()} and said [${said}]`);

    /* A click is not a move: a box-moved signal marks the layout edited,
       which is a dirty file on the desktop and a splice into the room's
       document here. A drag of the same box is one. */
    const movedBy = (dx) =>
    {
        const x = M._tw_graph_box_x(0) + 6, y = M._tw_graph_box_y(0) + 6;

        M._tw_node_signals_clear();
        M._tw_node_canvas_press(x, y, 1, 1);
        M._tw_node_canvas_motion(x + dx, y);
        M._tw_node_canvas_release(x + dx, y, 1);

        let moved = 0;

        for (let i = 0; i < M._tw_node_signal_count(); i++)
            moved += M._tw_node_signal_kind(i) === 0;

        return moved;
    };

    for (const [dx, want] of [[0, 0], [40, 1]])
    {
        const moved = movedBy(dx);

        if (moved === want)
            process.stdout.write(`ok    a box pressed and let go ${dx} ` +
                                 `pixels over says it moved ${want} times\n`);
        else
            fail(`a box pressed and let go ${dx} pixels over said it ` +
                 `moved ${moved} times, not ${want}`);
    }

    /* Escape lets go of what is selected, and with nothing selected is the
       page's: canvasview.js leaves the key alone when the canvas says it
       did not take it, so the page can give the keyboard back. */
    {
        const held = M._tw_node_canvas_selected() >= 0;
        const first = M._tw_node_canvas_key(1);
        const second = M._tw_node_canvas_key(1);

        if (held && first === 1 && M._tw_node_canvas_selected() < 0 &&
            second === 0)
            process.stdout.write('ok    Escape lets go of the selection, ' +
                                 'and with nothing held is not taken\n');
        else
            fail(`Escape with a box selected took ${first} and left ` +
                 `${M._tw_node_canvas_selected()}; again took ${second}`);
    }

    const heldNames = () =>
    {
        const out = [];

        for (let i = 0; i < M._tw_node_canvas_selection_count(); i++)
            out.push(M.UTF8ToString(M._tw_graph_box_name(
                M._tw_node_canvas_selection_at(i))));

        return out.sort().join(' ');
    };

    const queued = () =>
    {
        const out = [];

        for (let i = 0; i < M._tw_node_signal_count(); i++)
            out.push(M._tw_node_signal_kind(i));

        return out;
    };

    /* Two boxes, the second by Shift and a click, held through a rebuild
       of the same text and through one with a node put in before them:
       by name, not by where they were in the list. */
    {
        const click = (b, mods) =>
        {
            const x = M._tw_graph_box_x(b) + 6, y = M._tw_graph_box_y(b) + 6;

            M._tw_node_canvas_press(x, y, 1, 1, mods);
            M._tw_node_canvas_release(x, y, 1);
        };

        click(1, 0);
        click(2, 1);

        const held = heldNames();
        const at = M._tw_node_canvas_selection_at(0);

        M.ccall('tw_graph_build', 'number', ['string'], [text]);

        const same = heldNames();

        M.ccall('tw_graph_build', 'number', ['string'],
                ['node aaa osc::simple { freq = 1; };\n' + text]);

        /* Only a test of names if the node put in renumbered them. */
        const moved = M._tw_node_canvas_selection_count() === 2 &&
                      M._tw_node_canvas_selection_at(0) !== at;

        if (held.split(' ').length === 2 && same === held &&
            heldNames() === held && moved)
            process.stdout.write('ok    a selection of two, one added with ' +
                                 'Shift, is held by name across rebuilds\n');
        else
            fail(`two selected [${held}] came back as [${same}], and with ` +
                 `a node before them as [${heldNames()}]`);

        M.ccall('tw_graph_build', 'number', ['string'], [text]);
        M.ccall('tw_graph_apply_layout', 'number', ['string'], [text]);
    }

    /* The keys the page sends: an arrow selects, Shift+arrow says the
       selection moved, Delete asks for it to go. */
    {
        M._tw_node_canvas_key(1, 0);
        M._tw_node_signals_clear();

        const arrow = M._tw_node_canvas_key(3, 0) === 1 &&
                      M._tw_node_canvas_selected() >= 0;
        const box = M._tw_node_canvas_selected();
        const x0 = M._tw_graph_box_x(box);

        M._tw_node_signals_clear();

        const nudged = M._tw_node_canvas_key(3, 1) === 1 &&
                       queued().includes(9) &&
                       M._tw_graph_box_x(box) === x0 + 10;

        M._tw_node_signals_clear();

        const deleting = M._tw_node_canvas_key(6, 0) === 1 &&
                         queued().includes(10);

        if (arrow && nudged && deleting)
            process.stdout.write('ok    an arrow selects, Shift+arrow moves ' +
                                 'it 10 and says so, Delete asks\n');
        else
            fail(`arrow ${arrow}, nudge ${nudged}, delete ${deleting}`);

        M._tw_node_signals_clear();
    }

    /* A click on a wire selects it and cuts nothing; Delete then asks for
       it to be cut. At the middle of the curve, which with its handles
       level either side is halfway between its ends -- the first wire
       whose middle is clear of every box. */
    {
        M._tw_node_canvas_key(1, 0);

        let wire = -1, cutOnClick = false;

        for (let e = 0; e < M._tw_graph_edge_count() && wire < 0; e++)
        {
            const fb = M._tw_graph_edge_from_box(e);
            const tb = M._tw_graph_edge_to_box(e);
            /* A port's place is an offset from its box's corner. */
            const px = (b, p) => M._tw_graph_box_x(b) + M._tw_graph_port_x(b, p);
            const py = (b, p) => M._tw_graph_box_y(b) + M._tw_graph_port_y(b, p);
            const x = (px(fb, M._tw_graph_edge_from_port(e)) +
                       px(tb, M._tw_graph_edge_to_port(e))) / 2;
            const y = (py(fb, M._tw_graph_edge_from_port(e)) +
                       py(tb, M._tw_graph_edge_to_port(e))) / 2;

            M._tw_node_signals_clear();
            M._tw_node_canvas_press(x, y, 1, 1, 0);
            M._tw_node_canvas_release(x, y, 1);

            cutOnClick ||= queued().includes(4);

            if (queued().includes(11))
                wire = e;
        }

        M._tw_node_signals_clear();

        const asked = wire >= 0 && M._tw_node_canvas_key(6, 0) === 1 &&
                      queued().includes(4) &&
                      M._tw_node_signal_a(queued().indexOf(4)) === wire;

        if (wire >= 0 && !cutOnClick && asked)
            process.stdout.write('ok    a click on a wire selects it and ' +
                                 'cuts nothing; Delete asks for it cut\n');
        else
            fail(`wire ${wire}, cut on a click ${cutOnClick}, Delete ` +
                 `asked ${asked}`);

        M._tw_node_signals_clear();
    }
}

/* ---- a new patch, and a node added to it -------------------------------- */

{
    const made = edit('tw_edit_create', ['string', 'string'],
                      ['scratch', 'nodecheck']);

    if (made.r !== OK || M.ccall('tw_graph_build', 'number', ['string'],
                                 [made.text]) <= 0)
        fail('a new patch does not build');
    else
    {
        const added = edit('tw_edit_add_node',
                           ['string', 'string', 'string'],
                           [made.text, 'osc1', 'osc::simple']);
        const built = added.r === OK
            ? M.ccall('tw_graph_build', 'number', ['string'], [added.text])
            : -1;

        let found = false;

        for (let b = 0; b < built; b++)
            if (call('tw_graph_box_name', ['number'], [b]) === 'osc1')
                found = true;

        if (!found)
            fail(`a node added to a new patch is not in it (${added.why})`);
        else
            process.stdout.write(
                'ok    a new patch takes a node, with the plugin\'s own ' +
                'defaults\n');
    }
}

/* ---- probes: a tap, a module, and a panel -------------------------------
 *
 * The whole of what a probe is, end to end and headless. A .dsp is loaded
 * on a channel and a note played; the synth taps one arg of one node and
 * publishes what it sees; the samples are fed to a visual module, which
 * draws; and the canvas has a panel for it, which is a box like any other
 * and is drawn with the rest of the graph.
 *
 * In a page these are three instances of this module -- the worklet taps,
 * the page displays -- and the samples cross as a message. Here they are
 * one instance and a pointer, which is the same path with the hop taken
 * out: what is under test is that the tap publishes, the module draws, and
 * the canvas makes room for it.
 */
{
    const RATE = 48000;
    const file = path.join(dspDir, 'ts1.dsp');
    const patch = fs.readFileSync(file, 'utf8');

    M._tw_create(RATE, 256, 128);
    M.ccall('tw_load', 'number', ['number', 'string'], [0, patch]);

    /* The visual modules this build has. */
    const visuals = [];

    for (let i = 0; i < M._tw_visual_count(); i++)
        visuals.push(M.UTF8ToString(M.ccall('tw_visual_name', 'number',
                                            ['number'], [i])));

    if (visuals.length === 0)
        fail('the module has no visual modules in it');
    else
        process.stdout.write(
            `ok    the bundle has ${visuals.length} visual modules: ` +
            `${visuals.join(', ')}\n`);

    /* Somewhere to tap: a node with an output, which is what a probe
       reads and what a panel hangs on. The graph is asked rather than a
       name written here, since it is the thing that knows. */
    M.ccall('tw_graph_build', 'number', ['string'], [patch]);

    let where = null;

    for (let b = 0; b < M._tw_graph_box_count() && where === null; b++)
    {
        if (M._tw_graph_box_kind(b) !== 0)
            continue;

        for (let p = 0; p < M._tw_graph_port_count(b); p++)
            if (!M._tw_graph_port_is_input(b, p))
            {
                where = {
                    node: call('tw_graph_box_name', ['number'], [b]),
                    arg: call('tw_graph_port_name', ['number', 'number'],
                              [b, p]),
                };
                break;
            }
    }

    if (where === null)
    {
        fail('nothing in ts1.dsp has an output to probe');
        process.exitCode = failures;
        throw new Error('no output to probe');
    }

    const slot = M.ccall('tw_probe_arm', 'number',
                         ['number', 'string', 'string'],
                         [0, where.node, where.arg]);

    if (slot < 0)
        fail(`arming a probe on ${where.node}.${where.arg} said ` +
             `"${M.UTF8ToString(M._tw_probe_why())}"`);
    else
    {
        /* A note, and a second of it. */
        M._tw_note_on(-1, 0, 60, 100);

        let got = 0;

        for (let i = 0; i < RATE / 128 && got < 1024; i++)
        {
            M._tw_render(128);
            got += M._tw_probe_read(slot);
        }

        if (got === 0)
        {
            fail('an armed probe published nothing over a second of a note');
        }
        else
        {
            process.stdout.write(
                `ok    a probe on ${where.node}.${where.arg} published ` +
                `${got} samples\n`);

            /* Into a display, and drawn. */
            const display = M.ccall('tw_probe_open', 'number',
                                    ['string', 'string', 'string', 'number',
                                     'number'],
                                    ['scope', where.node, where.arg, -1,
                                     RATE]);

            if (display < 0)
                fail('visual/scope would not open');
            else
            {
                const at = M._tw_probe_buffer();
                const many = Math.min(got, M._tw_probe_buffer_size());

                M.HEAPF32.set(
                    M.HEAPF32.subarray(M._tw_probe_samples() >> 2,
                                       (M._tw_probe_samples() >> 2) + many),
                    at >> 2);
                M._tw_probe_feed(display, at, many);

                const words = M._tw_probe_draw(display, 128, 80);

                if (words <= 0)
                    fail(`a scope fed ${many} samples drew ${words} words`);
                else
                    process.stdout.write(
                        `ok    a scope fed ${many} samples draws ` +
                        `${words} words of ops\n`);

                /* And the canvas makes a panel for it: a box like any
                   other, drawn with the graph. */
                M.ccall('tw_graph_build', 'number', ['string'], [patch]);

                const before = M._tw_graph_box_count();
                const panel = M.ccall('tw_probe_panel', 'number',
                                      ['string', 'string', 'string',
                                       'number'],
                                      [where.node, where.arg, 'scope',
                                       92]);

                if (panel < 0 || M._tw_graph_box_count() !== before + 1 ||
                    M._tw_graph_box_kind(panel) !== 4)
                    fail(`a probe panel came back as box ${panel}, kind ` +
                         `${M._tw_graph_box_kind(panel)}`);
                else
                {
                    M._tw_probe_box(display, panel);
                    M._tw_node_canvas_viewport(0, 0, 900, 600);
                    M._tw_node_canvas_zoom_to_fit();

                    const drew = M._tw_node_canvas_draw(
                        M._tw_node_canvas_width(),
                        M._tw_node_canvas_height());

                    if (drew > 0)
                        process.stdout.write(
                            'ok    and the canvas draws it as a panel on ' +
                            `${where.node}, ${drew} words with it\n`);
                    else
                        fail('the canvas drew nothing with a panel on it');
                }

                M._tw_probe_close(display);
            }
        }

        M._tw_probe_disarm(slot);
    }
}

/* ---- and the layout block ----------------------------------------------- */

{
    const file = path.join(dspDir, 'ts1.dsp');
    const text = fs.readFileSync(file, 'utf8');

    M.ccall('tw_graph_build', 'number', ['string'], [text]);
    M.ccall('tw_graph_apply_layout', 'number', ['string'], [text]);

    const wrote = M.ccall('tw_layout_write', 'number', ['string'], [text]);
    const written = M.UTF8ToString(M._tw_edit_text());

    /* Written from the graph, so every box that is not an attached control
       has a line -- and the patch still builds from it. */
    const lines = written.split('\n').filter(
        (l) => l.startsWith('# @layout ')).length;

    if (!wrote || lines === 0 ||
        M.ccall('tw_graph_build', 'number', ['string'], [written]) <= 0)
    {
        fail(`the layout block came back with ${lines} positions`);
    }
    else
    {
        /* And it is the block the desktop writes, byte for byte: a drag in
           a browser moves a node for everybody, including whoever opens
           the file in the editor afterwards. */
        const want = execFileSync(dspedit,
                                  [file, 'layout-write', '-p',
                                   path.join(native, 'plugins') + '/'],
                                  { encoding: 'utf8' });

        if (written !== want)
            fail('the layout block the module writes is not the one the ' +
                 'desktop writes');
        else
            process.stdout.write(
                `ok    ts1.dsp's layout block is ${lines} positions, the ` +
                'same the desktop writes\n');
    }
}

/* ---- the panel's text rows: the header, a Name, arithmetic ------------- */

{
    const fixture =
        'name "P";\nauthor "A";\ncategory "Bass";\ndescription "D";\n' +
        '@lvl = 0.5;\n@lvl.widget = 1;\n@lvl.min = 0;\n@lvl.max = 1;\n' +
        'node ionode { channels = 1; out0 = osc->out; play = 1; };\n' +
        'node osc osc::simple { freq = ionode->note; amp = @lvl*0.5; };\n' +
        'io ionode;\n' +
        '# @layout osc 300 200\n# @layout @lvl 20 20\n';
    const file = path.join(fs.mkdtempSync(path.join(
        fs.realpathSync(process.env.TMPDIR ?? '/tmp'), 'nodecheck-')),
        'text.dsp');

    fs.writeFileSync(file, fixture);

    const boxOf = (name) =>
    {
        for (let b = 0; b < M._tw_graph_box_count(); b++)
            if (call('tw_graph_box_name', ['number'], [b]) === name)
                return b;

        return -1;
    };

    const panelOf = (box) =>
        M._tw_panel_open(NODE_VALUE, box, 0) === 0
            ? null : JSON.parse(M.UTF8ToString(M._tw_panel_json()));

    M.ccall('tw_graph_build', 'number', ['string'], [fixture]);

    const osc = panelOf(boxOf('osc'));
    const row = (p, id) => p?.rows.find((r) => r.id === id);
    const header = panelOf(-1);

    if (row(osc, '#name')?.text !== 'osc' ||
        row(osc, 'amp')?.text !== '@lvl*0.5' || !row(osc, 'amp')?.editable)
        fail('a node\'s panel has its Name and its arithmetic as the file ' +
             'spells it');
    else if (header?.title !== 'This patch' ||
             row(header, 'author')?.text !== 'A')
        fail('with nothing selected the panel is the patch\'s header');
    else
        process.stdout.write('ok    the panel offers a Name, arithmetic in ' +
                             'the file\'s spelling, and the header\n');

    /* thPanelEdit::Kind, and what an edit of each row becomes. */
    const NODE_INFO = 4, NODE_RENAME = 5, NODE_EXPRESSION = 6, ECHO = -2;
    const intents = [
        ['a header field', -1, 'author', 'B', NODE_INFO, '', 'author'],
        ['a name', boxOf('osc'), '#name', 'voice', NODE_RENAME, '', '#name'],
        ['arithmetic', boxOf('osc'), 'amp', '@lvl*0.25', NODE_EXPRESSION,
         'osc', 'amp'],
        ['a name typed back', boxOf('osc'), '#name', 'osc', ECHO, null, null],
    ];

    for (const [what, box, id, typed, kind, node, field] of intents)
    {
        const got = M.ccall('tw_node_propose', 'number',
                            ['number', 'string', 'string', 'string'],
                            [box, id, typed, fixture]);

        if (got !== kind ||
            (node !== null &&
             (M.UTF8ToString(M._tw_node_intent_node()) !== node ||
              M.UTF8ToString(M._tw_node_intent_row()) !== field)))
            fail(`propose: ${what} is ${got}, not ${kind}`);
    }

    /* Typed back over a document that has moved on since the build -- a
       peer's half-typed line -- it is an edit, since the splice writes over
       that document and not the one the panel shows. */
    const moved = M.ccall('tw_node_propose', 'number',
                          ['number', 'string', 'string', 'string'],
                          [boxOf('osc'), '#name', 'osc', fixture + 'node ']);

    if (moved !== NODE_RENAME)
        fail(`a name typed back over a moved document is ${moved}, not an ` +
             'edit');

    /* Each edit the desktop's, byte for byte. */
    const edits = [
        ['tw_edit_set_info', ['string', 'string'], ['author', 'B'],
         ['set-info', 'author', 'B']],
        ['tw_edit_rename', ['number', 'string', 'string'], [0, 'osc', 'voice'],
         ['rename-node', 'osc', 'voice']],
        ['tw_edit_rename', ['number', 'string', 'string'], [1, 'lvl', 'gain'],
         ['rename-control', 'lvl', 'gain']],
        ['tw_edit_set_expression', ['string', 'string', 'string'],
         ['osc', 'amp', '@lvl * 0.25'],
         ['set-expression', 'osc', 'amp', '@lvl * 0.25']],
    ];
    let same = 0;

    for (const [name, types, args, op] of edits)
    {
        const got = edit(name, ['string', ...types], [fixture, ...args]);
        const want = execFileSync(dspedit, [file, ...op], { encoding: 'utf8' });

        if (got.r !== OK || got.text !== want)
            fail(`${name} ${args.join(' ')} is not the desktop's edit ` +
                 `(${got.why})`);
        else
            same++;
    }

    if (same === edits.length)
        process.stdout.write(`ok    the header, a rename and arithmetic are ` +
                             'the edits the desktop makes\n');

    const bad = edit('tw_edit_set_expression',
                     ['string', 'string', 'string', 'string'],
                     [fixture, 'osc', 'amp', '@lvl *']);

    if (bad.r === OK || bad.why !== 'that does not parse')
        fail(`arithmetic that does not parse is refused (${bad.why})`);

    /* The selection is held by name across a build, and a rename is the
       name changing. */
    M._tw_node_canvas_viewport(0, 0, 900, 600);
    M._tw_node_canvas_select(boxOf('osc'));

    const renamed = edit('tw_edit_rename', ['string', 'number', 'string',
                                            'string'],
                         [fixture, 0, 'osc', 'voice']);

    M.ccall('tw_graph_build', 'number', ['string'], [renamed.text]);

    const held = M._tw_node_canvas_selected();

    if (held < 0 || call('tw_graph_box_name', ['number'], [held]) !== 'voice')
        fail('a renamed box is still the one selected');
    else
        process.stdout.write('ok    a renamed box is still the one ' +
                             'selected\n');

    fs.rmSync(path.dirname(file), { recursive: true, force: true });
}

/* ---- a control's range, label and group, for its form ---------------- */

{
    const fixture =
        '@a = 0.5;\n@a.widget = 1;\n@a.min = 0.25;\n@a.max = 2;\n' +
        '@a.label = "Cut off";\n@a.group = "Filter";\n' +
        '@b = 1;\n@b.widget = 1;\n@b.min = 0;\n@b.max = 4;\n' +
        'node ionode { channels = 1; out0 = osc->out; play = 1; };\n' +
        'node osc osc::simple { freq = ionode->note; amp = @a; mul = @b; };\n' +
        'io ionode;\n';

    M.ccall('tw_graph_build', 'number', ['string'], [fixture]);

    const of = (name) =>
    {
        for (let b = 0; b < M._tw_graph_box_count(); b++)
            if (call('tw_graph_box_name', ['number'], [b]) === name)
                return {
                    min: M._tw_graph_box_control_min(b),
                    max: M._tw_graph_box_control_max(b),
                    label: call('tw_graph_box_control_label', ['number'], [b]),
                    group: call('tw_graph_box_control_group', ['number'], [b]),
                };

        return null;
    };
    const a = of('@a'), b = of('@b');

    /* The label as the file says it: none is none, not the name the strip
       falls back to, or a form accepted untouched would write one. */
    if (a?.min !== 0.25 || a?.max !== 2 || a?.label !== 'Cut off' ||
        a?.group !== 'Filter' || b?.label !== '' || b?.group !== '')
        fail(`a control's form starts from what the file says: ` +
             JSON.stringify([a, b]));
    else
        process.stdout.write('ok    a control\'s range, label and group are ' +
                             'what the file says, an absent label empty\n');
}

/* ---- a node added where it is looked at, and the graph arranged ------ */

{
    const fixture =
        'node ionode { channels = 1; out0 = osc->out; play = 1; };\n' +
        'node osc osc::simple { freq = ionode->note; };\n' +
        'io ionode;\n';
    const boxOf = (name) =>
    {
        for (let b = 0; b < M._tw_graph_box_count(); b++)
            if (call('tw_graph_box_name', ['number'], [b]) === name)
                return b;

        return -1;
    };

    M.ccall('tw_graph_build', 'number', ['string'], [fixture]);
    M._tw_node_canvas_viewport(0, 0, 900, 600);
    M._tw_node_canvas_set_zoom(1);

    /* Two added one after the other at the same view: the second steps off
       the first rather than landing on it, and each is the one selected. */
    let text = fixture;
    const at = [];

    for (const name of ['lfo', 'lfo2'])
    {
        text = edit('tw_edit_add_node', ['string', 'string', 'string'],
                    [text, name, 'osc::simple']).text;
        M.ccall('tw_graph_build', 'number', ['string'], [text]);
        M.ccall('tw_graph_apply_layout', 'number', ['string'], [text]);

        const placed = M.ccall('tw_graph_place', 'number', ['string'],
                               [name]);
        const b = boxOf(name);

        at.push({ placed, selected: M._tw_node_canvas_selected() === b,
                  x: M._tw_graph_box_x(b), y: M._tw_graph_box_y(b) });

        /* As the page does after a place: where it went, into the file. */
        if (M.ccall('tw_layout_write', 'number', ['string'], [text]))
            text = M.UTF8ToString(M._tw_edit_text());
    }

    const [one, two] = at;

    if (!one.placed || !one.selected || !two.selected ||
        Math.abs(one.x + 100 - 450) > 100 || Math.abs(one.y - 300) > 200 ||
        (two.x === one.x && two.y === one.y))
        fail(`a node added is put in the view and selected: ` +
             JSON.stringify(at));
    else
        process.stdout.write('ok    a node added goes where the view is, ' +
                             'beside one already there, and is selected\n');

    /* Arranged: the computed layout again, the selection kept. */
    const laid = boxOf('osc');
    const was = { x: M._tw_graph_box_x(laid), y: M._tw_graph_box_y(laid) };

    M._tw_node_canvas_select(laid);
    M._tw_graph_arrange();

    const fresh = { x: M._tw_graph_box_x(boxOf('lfo2')),
                    y: M._tw_graph_box_y(boxOf('lfo2')) };

    M.ccall('tw_graph_build', 'number', ['string'], [text]);

    if (M._tw_node_canvas_selected() !== boxOf('osc') ||
        fresh.x !== M._tw_graph_box_x(boxOf('lfo2')) ||
        fresh.y !== M._tw_graph_box_y(boxOf('lfo2')) ||
        was.x !== M._tw_graph_box_x(boxOf('osc')))
        fail('Arrange puts every box where the layout does, and keeps the ' +
             'selection');
    else
        process.stdout.write('ok    Arrange puts every box where the layout ' +
                             'does, the selection kept\n');
}

/* ---- a control's strip, hovered --------------------------------------- */

{
    const fixture =
        '@lvl = 0.5;\n@lvl.widget = 1;\n@lvl.min = 0;\n@lvl.max = 1;\n' +
        '@lvl.label = "Level of the oscillator";\n' +
        'node ionode { channels = 1; out0 = osc->out; play = 1; };\n' +
        'node osc osc::simple { freq = ionode->note; amp = @lvl; };\n' +
        'io ionode;\n';

    M.ccall('tw_graph_build', 'number', ['string'], [fixture]);
    M._tw_node_canvas_viewport(0, 0, 900, 600);
    M._tw_node_canvas_set_zoom(1);

    let strip = -1;

    for (let b = 0; b < M._tw_graph_box_count(); b++)
        if (call('tw_graph_box_name', ['number'], [b]) === '@lvl')
            strip = b;

    const tip = (x, y) => call('tw_node_canvas_tip', ['number', 'number'],
                               [x, y]);
    const over = tip(M._tw_graph_box_x(strip) + 4,
                     M._tw_graph_box_y(strip) + 4);

    /* Spelled as the panels spell a value with this range. */
    if (over !== 'Level of the oscillator (@lvl): 0.5000, from 0.0000 to ' +
                 '1.0000' ||
        tip(-1, -1) !== '')
        fail(`a control's strip, hovered, says all of itself: "${over}"`);
    else
        process.stdout.write('ok    a control\'s strip, hovered, says its ' +
                             'whole label, value and range\n');
}

/* ---- a probe kept in the file ---------------------------------------- */

{
    const fixture =
        'node ionode { channels = 1; out0 = osc->out; play = 1; };\n' +
        'node osc osc::simple { freq = ionode->note; };\n' +
        'io ionode;\n' +
        '# @layout osc 400 300\n' +
        '# @probe osc out scope\n';
    const boxOf = (name) =>
    {
        for (let b = 0; b < M._tw_graph_box_count(); b++)
            if (call('tw_graph_box_name', ['number'], [b]) === name &&
                M._tw_graph_box_kind(b) !== 4)
                return b;

        return -1;
    };

    M.ccall('tw_graph_build', 'number', ['string'], [fixture]);
    M.ccall('tw_graph_apply_layout', 'number', ['string'], [fixture]);

    const lines = M.ccall('tw_layout_probe_count', 'number', ['string'],
                          [fixture]);
    const read = lines === 1 ? [call('tw_layout_probe_node', ['number'], [0]),
                                call('tw_layout_probe_arg', ['number'], [0]),
                                call('tw_layout_probe_visual', ['number'], [0])]
                             : [];

    /* The panel hung on its node leaves every box where the file put it:
       laying the graph out again for the panel used to put them all back
       where the layout would. */
    const panel = M.ccall('tw_probe_panel', 'number',
                          ['string', 'string', 'string', 'number'],
                          ['osc', 'out', 'scope', 0]);
    const kept = M._tw_graph_box_x(boxOf('osc')) === 400 &&
                 M._tw_graph_box_y(boxOf('osc')) === 300;
    const written = M.ccall('tw_layout_write', 'number', ['string'],
                            [fixture]) ? M.UTF8ToString(M._tw_edit_text()) : '';

    M._tw_probe_unpanel(panel);

    const rewrote = M.ccall('tw_layout_write', 'number', ['string'],
                            [fixture]);
    const gone = M.UTF8ToString(M._tw_edit_text());

    if (read.join(' ') !== 'osc out scope' || panel < 0 || !kept ||
        !written.includes('# @probe osc out scope') ||
        !written.includes('# @layout osc 400 300') || !rewrote ||
        gone.includes('# @probe'))
        fail(`a probe is read from its line, kept by a panel, and gone with ` +
             `it: ${JSON.stringify({ read, panel, kept })}`);
    else
        process.stdout.write('ok    a probe is read from its line; its panel ' +
                             'keeps the boxes where they were, and taken ' +
                             'off, its line goes\n');
}

/* Whether a text builds, for a room's Undo to refuse one that would not:
   asked without the graph on screen becoming the text asked about. */
{
    const read = (n) => fs.readFileSync(path.join(dspDir, n), 'utf8');
    const shown = read('pluck.dsp');
    const other = read('ladder.dsp');
    const count = M.ccall('tw_graph_build', 'number', ['string'], [shown]);
    const asked = [shown, other, `${shown}\nnode broken {`].map((t) =>
        M.ccall('tw_graph_parses', 'number', ['string'], [t]));

    log.length = 0;

    if (JSON.stringify(asked) !== '[1,1,0]' ||
        M._tw_graph_box_count() !== count)
        fail(`tw_graph_parses: ${JSON.stringify(asked)}, and the graph ` +
             `shown has ${M._tw_graph_box_count()} boxes of ${count}`);
    else
        process.stdout.write('ok    a text that builds parses and one that ' +
                             'does not does not, and the graph shown stays ' +
                             'as it was\n');
}

process.stdout.write(failures === 0
    ? '\nthe node editor\'s model runs in the browser, and edits the way ' +
      'the desktop edits\n'
    : `\n${failures} failed\n`);

process.exitCode = failures;
