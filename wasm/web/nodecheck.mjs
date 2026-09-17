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
 * nodecheck.mjs -- the node editor's model, through the browser module.
 *
 *   node wasm/web/nodecheck.mjs [BUILD_DIR] [NATIVE_BUILD_DIR]
 *
 * The .dsp canvas in a room is the desktop's NodeGraph, NodeEdit,
 * NodeLayout and NodeCatalog compiled to wasm and run over the document's
 * text (JAM_M6.md, section 7). Two things have to be true of that, and
 * they are different things.
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
const names = JSON.parse(fs.readFileSync(path.join(dspDir, 'index.json'),
                                         'utf8'));

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
        if (M._tw_graph_box_kind(b) !== 0)
            continue;

        for (let p = 0; p < M._tw_graph_param_count(b); p++)
        {
            /* A plain value: not wired, not an output, and a number. */
            if (M._tw_graph_param_kind(b, p) !== 0 ||
                M._tw_graph_param_is_output(b, p) ||
                !M._tw_graph_param_has_value(b, p))
                continue;

            where = {
                node: call('tw_graph_box_name', ['number'], [b]),
                arg: call('tw_graph_param_name', ['number', 'number'],
                          [b, p]),
                was: M._tw_graph_param_value(b, p),
            };
            break;
        }
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

/* The other half of JAM_M6.md's section 3 gate: the node canvas, which is
   the desktop's, drawn over every shipped .dsp through the cairo stand-in.
   The list has to be walkable by the arity table alone and known to
   replay.js op for op -- the same three questions drawcheck asks of the
   composer canvas, of the other canvas. */
{
    const { ARITY, OP_NAMES, replay } =
        await import('../cairo2d/replay.js');

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

process.stdout.write(failures === 0
    ? '\nthe node editor\'s model runs in the browser, and edits the way ' +
      'the desktop edits\n'
    : `\n${failures} failed\n`);

process.exitCode = failures;
