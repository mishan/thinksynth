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
 * nodeview.js -- the .dsp canvas, over the room's document.
 *
 * The page's half of the node editor. The canvas, the graph, the layout
 * and every edit are the desktop's C++ compiled to wasm and run in the
 * page's own instance of the module (thinknode.cpp); what is here is the
 * element, the pointer, the two forms -- the palette and the params panel
 * -- and the one thing that is genuinely a room's business: an edit is a
 * splice into the shared document.
 *
 * WHAT AN EDIT IS. The canvas decides what a gesture meant and says so;
 * this asks the module for the patch that gesture implies, and hands the
 * new text to whoever owns the file.
 *
 * WHO THAT IS is the one thing that differs between the two pages, so it
 * is a parameter: `files' reads a .dsp by name, writes one back, and says
 * when one changed under it. In a room that is the shared document -- the
 * write is a splice into a Y.Text, nobody's copy is authoritative and
 * there is no save, and the piece plays the new text at the next Apply.
 * On the solo page it is the patch in the textarea, or one of the pieces'
 * instruments, and the write reloads what is playing. The canvas, the
 * graph and every edit are the same on both.
 *
 * The forms are HTML for the same reason they are gtkmm on the desktop: a
 * form is the platform's. What is in them -- which args a node has, what a
 * plugin's ports are, why an edit was refused -- comes from the model, on
 * both.
 */

import { createCanvasView } from './canvasview.js';
import { placePopover } from './popover.js';
import { CHOICE, numberIn, showPanel } from './panel.js';

/* NodeEdit::Result::OK, and the signal kinds thinknode.cpp queues. */
const OK = 0;

/* thPanel::Kind, for the panel over the selected box. */
const NODE_VALUE = 3;

const SIG = {
    BOX_MOVED: 0,
    SELECTED: 1,
    SELECTION: 2,
    CONNECT: 3,
    DISCONNECT: 4,
    REFUSED: 5,
    CONTROL: 6,
    CONTEXT: 7,
    PROBE: 8,
};

/* NodeGraph::Box kinds, as tw_graph_box_kind reports them. */
const BOX = { NODE: 0, CONTROL: 1, IO_IN: 2, IO_OUT: 3, PROBE: 4 };

export async function createNodeView ({ files, root = document,
                                        onStatus = () => {},
                                        probe = null, unprobe = null,
                                        sampleRate = 48000 })
{
    const $ = (id) => root.getElementById(id);

    /* The page's own instance of the module. The worklet's is playing and
       the mirror's is composing; this one only ever reads and edits text,
       and it fetches its own wasm because a page can. */
    const { default: createThinkWeb } = await import('./thinkweb.js');

    const log = [];
    const M = await createThinkWeb({
        print: (s) => log.push(s),
        printErr: (s) => log.push(s),
    });

    M._tw_catalog_take();

    let file = null;            /* the .dsp this is showing */
    let watching = null;        /* how to stop watching it change */
    let selected = -1;

    /* The probes armed on this patch: the slot the worklet gave back, the
       display this instance opened for it, and what it is watching. A
       probe is not in the file -- it is a display -- so these are rebuilt
       against the graph after every change. */
    const probes = [];

    /* Which channel the piece put this instrument on, and so which one a
       tap is armed on. Set by the page when it knows. */
    let channel = -1;

    const text = () => (file === null ? '' : files.read(file) ?? '');

    const call = (name, types, args) =>
        M.UTF8ToString(M.ccall(name, 'number', types, args));

    /* Every edit is the same three steps: ask the module what the patch
       becomes, splice the difference into the document, and say why if it
       would not. The rebuild comes back round through the observer, since
       an edit of anyone else's arrives that way too. */
    const edit = (name, types, args) =>
    {
        const r = M.ccall(name, 'number', types, [text(), ...args]);

        if (r !== OK)
        {
            onStatus(`${call('tw_edit_result_text', ['number'], [r])}: ` +
                     M.UTF8ToString(M._tw_edit_why()));
            return false;
        }

        files.write(file, M.UTF8ToString(M._tw_edit_text()));

        return true;
    };

    /* ---- the canvas ---- */

    const view = createCanvasView({
        scroller: $('nodescroll'),
        canvas: $('nodecanvas'),
        send: (m) => toModule(m),

        /* At 1:1, and Fit for the whole graph: see canvasview.js. On a
           phone the other way round -- 1:1 there is a strip of one box at
           a time out of a graph six screens wide, and the whole graph
           small is at least a map of it. */
        fitOnShow: matchMedia('(max-width: 40em)').matches,
    });

    /* The shell speaks messages so that the composer view's shell and this
       one are the same file; here they are answered in the same tick
       rather than by a worker. */
    function toModule (m)
    {
        switch (m.type)
        {
            case 'view':
                M._tw_node_canvas_viewport(m.x, m.y, m.w, m.h);

                if (m.fit)
                    M._tw_node_canvas_zoom_to_fit();

                break;

            case 'press':
                M._tw_node_canvas_press(m.x, m.y, m.button ?? 1,
                                        m.nPress ?? 1);
                break;

            case 'motion':
                M._tw_node_canvas_motion(m.x, m.y);
                break;

            case 'release':
                M._tw_node_canvas_release(m.x, m.y, m.button ?? 1);
                break;

            case 'key':
                M._tw_node_canvas_key(1);       /* CanvasContent::KEY_ESCAPE */
                break;

            case 'zoomBy':
                M._tw_node_canvas_set_zoom(M._tw_node_canvas_zoom() * m.by);
                break;

            case 'draw':
                paint();
                return;
        }

        drain();
    }

    /* One frame, straight out of the heap: the same three tables the
       composer view's worker posts over, read here without a hop. */
    function paint ()
    {
        const w = M._tw_node_canvas_width();
        const h = M._tw_node_canvas_height();

        if (w <= 0 || h <= 0)
            return;

        const words = M._tw_node_canvas_draw(w, h);

        if (words <= 0)
            return;

        const at = M._tw_draw_ops();
        const ops = M.HEAPF32.subarray(at >> 2, (at >> 2) + words);
        const strings = [];

        for (let i = 0; i < M._tw_draw_string_count(); i++)
            strings.push(M.UTF8ToString(M._tw_draw_string(i)));

        view.frame({ ops, strings, surfaces: [], w, h,
                     width: w, height: h });
    }

    /* ---- what the canvas decided ----
     *
     * The desktop's NodeEditor answers these with an edit, a rebuild or a
     * line in the status bar. So does this.
     */
    function drain ()
    {
        const many = M._tw_node_signal_count();

        /* Read out and cleared before a single one is acted on, because
           acting on one re-enters this function: an edit writes the file,
           the document's observer fires, and the rebuild that follows
           drains again. Cleared afterwards, that second drain found the
           same signals still queued and applied them a second time --
           against a graph whose edges the first edit had just renumbered,
           so a cut wire took an unrelated one with it on every peer. */
        const queue = [];

        for (let i = 0; i < many; i++)
            queue.push({
                kind: M._tw_node_signal_kind(i),
                a: M._tw_node_signal_a(i),
                b: M._tw_node_signal_b(i),
                c: M._tw_node_signal_c(i),
                d: M._tw_node_signal_d(i),
                value: M._tw_node_signal_value(i),
                x: M._tw_node_signal_x(i),
                y: M._tw_node_signal_y(i),
                text: M.UTF8ToString(M._tw_node_signal_text(i)),
            });

        if (many > 0)
            M._tw_node_signals_clear();

        for (const sig of queue)
        {
            const kind = sig.kind;
            const a = sig.a;

            switch (kind)
            {
                case SIG.SELECTED:
                    selected = a;
                    showParams();
                    break;

                /* A wire: from a box's port to another's. The graph knows
                   which end is which; the names are what the file wants. */
                case SIG.CONNECT:
                    connect(a, sig.b, sig.c, sig.d);
                    break;

                /* A wire cut. What the file needs is the input it
                   arrived at: `in = 0;' and no line at all are the same
                   thing to the engine, and keeping the line preserves its
                   comment and makes a reconnect restore the file exactly
                   (NodeEdit::disconnect). */
                case SIG.DISCONNECT:
                    disconnect(a);
                    break;

                /* A right-click: what can be done here. Probing is the
                   one thing the canvas does not know about, since the
                   modules and the channel are somebody else's -- so the
                   answer is here, as it is in NodeEditor. */
                case SIG.CONTEXT:
                    offerProbe(a, sig.b, sig.x, sig.y);
                    break;

                case SIG.REFUSED:
                    onStatus(sig.text);
                    break;

                /* A control's slider: live while it is dragged, spliced
                   once when it is let go, so a drag across the track is
                   one edit and not fifty. */
                case SIG.CONTROL:
                    if (sig.b === 1)
                        edit('tw_edit_set_chanarg', ['string', 'string',
                                                     'number'],
                             [call('tw_graph_box_control', ['number'], [a]),
                              sig.value]);
                    break;

                /* A box was dragged: the positions go back into the file's
                   own layout block, which is where the desktop keeps
                   them. */
                case SIG.BOX_MOVED:
                {
                    const was = text();

                    if (M.ccall('tw_layout_write', 'number', ['string'],
                                [was]))
                        files.write(file,
                                    M.UTF8ToString(M._tw_edit_text()));

                    break;
                }
            }
        }
    }

    /* The wire at `edge', cut. The lookup is the desktop's
       (NodeEditor::onDisconnect): an edge names two ports, and the one
       the file has a line for is the input end. */
    function disconnect (edge)
    {
        const box = M._tw_graph_edge_to_box(edge);
        const port = M._tw_graph_edge_to_port(edge);

        if (box < 0 || port < 0)
            return;

        const node = call('tw_graph_box_name', ['number'], [box]);
        const arg = call('tw_graph_port_name', ['number', 'number'],
                         [box, port]);

        if (edit('tw_edit_disconnect',
                 ['string', 'string', 'string', 'number'], [node, arg, 0]))
            onStatus(`Disconnected ${node}.${arg}.`);
    }

    /* A wire the canvas asked for, in the names the file uses. */
    function connect (fromBox, fromPort, toBox, toPort)
    {
        const toName = call('tw_graph_box_name', ['number'], [toBox]);
        const toArg = call('tw_graph_port_name', ['number', 'number'],
                           [toBox, toPort]);

        /* A control is spelled `@name' and a node `node->port'; the
           writer has one call for each, because guessing from a name that
           happens to start with an @ is the kind of cleverness that fails
           on one .dsp. */
        if (M._tw_graph_box_kind(fromBox) === BOX.CONTROL)
            edit('tw_edit_connect_control',
                 ['string', 'string', 'string', 'string'],
                 [toName, toArg,
                  call('tw_graph_box_control', ['number'], [fromBox])]);
        else
            edit('tw_edit_connect',
                 ['string', 'string', 'string', 'string', 'string'],
                 [toName, toArg,
                  call('tw_graph_box_name', ['number'], [fromBox]),
                  call('tw_graph_port_name', ['number', 'number'],
                       [fromBox, fromPort])]);
    }

    /* ---- probes ----
     *
     * Three things at once, in three places: a tap in the worklet on the
     * channel this instrument is loaded on, a display in this instance,
     * and a panel on the canvas. The samples come back with the tape.
     */

    /* A right-click on an output port: which display to watch it with,
       or stop watching it. The canvas says where the pointer was and on
       what, and nothing about the menu itself -- what can be done to a
       port is the editor's business on the desktop too. */
    function offerProbe (box, port, x, y)
    {
        const menu = $('nodemenu');

        menu.replaceChildren();

        if (probe === null || box < 0)
        {
            menu.hidden = true;
            return;
        }

        const item = (label, go) =>
        {
            const b = document.createElement('button');

            b.textContent = label;
            b.addEventListener('click', () =>
            {
                menu.hidden = true;
                go();
            });
            menu.append(b);
        };

        const title = (text) =>
        {
            const t = document.createElement('span');

            t.className = 'menutitle';
            t.textContent = text;
            menu.append(t);
        };

        const node = call('tw_graph_box_name', ['number'], [box]);
        const onPort = port >= 0 && !M._tw_graph_port_is_input(box, port);

        if (onPort)
        {
            /* A port: watch it, or stop if it is already watched. */
            const arg = call('tw_graph_port_name', ['number', 'number'],
                             [box, port]);
            const already = probes.findIndex((p) => p.node === node &&
                                                    p.arg === arg);

            title(`${node}.${arg}`);

            if (channel < 0)
                item('nothing is playing this instrument', () => {});
            else if (already >= 0)
                item('Stop watching', () => stopProbe(already));
            else
                for (let i = 0; i < M._tw_visual_count(); i++)
                {
                    const visual = call('tw_visual_name', ['number'], [i]);

                    item(`Watch with a ${visual}`,
                         () => armProbe(node, arg, visual));
                }
        }
        else
        {
            /* Not on a port: whatever this node is being watched with, to
               take it away. A panel sits on its host and their rectangles
               overlap, so the box under the pointer is the host as often
               as the panel -- and either is somebody saying "this one".
               Worth having its own way in: arming a probe makes the host
               taller and moves everything below it, so finding the same
               port a second time is not the easy thing it sounds like. */
            const mine = [];

            probes.forEach((p, i) =>
            {
                if (p.box === box || p.node === node)
                    mine.push(i);
            });

            if (mine.length === 0)
            {
                menu.hidden = true;
                return;
            }

            title(node);

            for (const i of mine)
                item(`Stop watching ${probes[i].node}.${probes[i].arg}`,
                     () => stopProbe(i));
        }

        /* Beside the pointer, in the page's own coordinates: the canvas
           said where in its own pixels, and the element says where it
           is. Held inside the window by placePopover, since a pane can
           be narrower than this menu is. */
        const at = $('nodecanvas').getBoundingClientRect();

        placePopover(menu, at.left + window.scrollX + x,
                     at.top + window.scrollY + y);
    }

    function stopProbe (which)
    {
        const p = probes[which];

        unprobe?.(p.slot);
        M._tw_probe_close(p.display);
        probes.splice(which, 1);
        onStatus(`Stopped watching ${p.node}.${p.arg}.`);
        rebuild();
    }

    async function armProbe (node, arg, visual)
    {
        if (probe === null || channel < 0)
            return;

        const { slot, why } = await probe(channel, node, arg);

        if (slot < 0)
        {
            onStatus(`${node}.${arg} cannot be probed: ${why}`);
            return;
        }

        const display = M.ccall('tw_probe_open', 'number',
                                ['string', 'string', 'string', 'number',
                                 'number'],
                                [visual, node, arg, -1, sampleRate]);

        if (display < 0)
        {
            unprobe?.(slot);
            onStatus(`the ${visual} module would not open`);
            return;
        }

        probes.push({ slot, display, node, arg, visual, box: -1 });
        onStatus(`Watching ${node}.${arg} with a ${visual}.`);
        rebuild();
    }

    /* The panels, put back after a rebuild: a probe is not in the patch,
       so building the graph from the text loses them every time and the
       page is what remembers. */
    function showProbes ()
    {
        for (const p of probes)
        {
            p.box = M.ccall('tw_probe_panel', 'number',
                            ['string', 'string', 'string', 'number'],
                            [p.node, p.arg, p.visual, 0]);

            M._tw_probe_box(p.display, p.box);
        }
    }

    /* Samples from the worklet, by slot: into the display, and a frame. */
    const feed = (taps) =>
    {
        let any = false;

        for (const tap of taps ?? [])
        {
            const p = probes.find((q) => q.slot === tap.slot);

            if (p === undefined || tap.samples.length === 0)
                continue;

            const at = M._tw_probe_buffer();
            const room = M._tw_probe_buffer_size();

            /* The newest, if more than a bufferful arrived: a display
               shows what is happening now. */
            const many = Math.min(tap.samples.length, room);
            const from = tap.samples.length - many;

            M.HEAPF32.set(tap.samples.subarray(from), at >> 2);
            M._tw_probe_feed(p.display, at, many);
            any = true;
        }

        if (any)
            paint();
    };

    /* ---- the forms ---- */

    /* The palette: every plugin this module was built with, by category.
       Adding one is an edit like any other -- a node with the plugin's own
       declared defaults in it. */
    function showPalette ()
    {
        const palette = $('nodepalette');

        palette.replaceChildren();

        for (let c = 0; c < M._tw_catalog_category_count(); c++)
        {
            const category = call('tw_catalog_category', ['number'], [c]);
            const group = document.createElement('optgroup');

            group.label = category;

            const many = M.ccall('tw_catalog_in_category', 'number',
                                 ['string'], [category]);

            for (let i = 0; i < many; i++)
            {
                const option = document.createElement('option');

                option.value = call('tw_catalog_spelling',
                                    ['string', 'number'], [category, i]);
                option.textContent = option.value;
                group.append(option);
            }

            palette.append(group);
        }
    }

    function addNode ()
    {
        const plugin = $('nodepalette').value;

        if (file === null || plugin === '')
            return;

        const name = call('tw_catalog_suggest', ['string'],
                          [plugin.replace(/^.*::/, '')]);

        if (edit('tw_edit_add_node', ['string', 'string', 'string'],
                 [name, plugin]))
            onStatus(`Added ${name} (${plugin}).`);
    }

    /* The params panel: the selected box's args, described by the module
     * (src/NodePanel.cpp) and drawn by the renderer the knobs and the
     * channel's parameters use.
     *
     * What each parameter is -- offered or only shown, a number or a list of
     * the plugin's own names, and what a wired one says instead of a value
     * -- is the panel's answer and not this file's. This used to be a
     * thinner version of the desktop's answer to the same question, built
     * from six C accessors, with no labels, no tooltips, no value names and
     * no ranges in it.
     *
     * An edit is a splice, which is why there is no delivery in the module
     * for one: a node's value is a number in the `.dsp', so the intent
     * becomes tw_edit_set_value and the new text goes to whoever owns the
     * file. A list's spelling is turned back into its number here, off the
     * row's own choices -- a lookup in what the module handed over, not a
     * second opinion about what the values are.
     */
    function showParams ()
    {
        const box = $('nodeparams');

        box.replaceChildren();

        if (selected < 0)
        {
            $('nodeselected').textContent = '';
            return;
        }

        const node = call('tw_graph_box_name', ['number'], [selected]);

        if (M._tw_panel_open(NODE_VALUE, selected, 0) === 0)
        {
            $('nodeselected').textContent = node;
            return;
        }

        const panel = JSON.parse(M.UTF8ToString(M._tw_panel_json()));

        $('nodeselected').textContent = `${panel.title} — ${panel.subtitle}`;

        showPanel(box, panel, (name, text) =>
        {
            const row = panel.rows.find((r) => r.id === name);

            /* Not Number(text): Number('') is 0 and Number(' ') is 0, so an
               emptied box spliced `arg = 0' into the .dsp and broadcast it
               to the room. A splice is the one delivery with no propose()
               behind it -- tw_panel_edit refuses a NODE_VALUE row, because
               what the intent becomes is a rewrite of the file rather than a
               write to an arg -- so this is the only place the text is
               looked at at all. */
            const value = row?.kind === CHOICE
                ? row.choices.find((c) => c.name === text)?.value
                : numberIn(text);

            if (value === undefined || value === null || Number.isNaN(value))
                return;

            edit('tw_edit_set_value',
                 ['string', 'string', 'string', 'number'],
                 [node, name, value]);
        });
    }

    /* ---- the file ---- */

    /* Built, laid out, and drawn. Called on opening a file and whenever
       the document's text for it changes -- which is both this page's
       edits and everybody else's. */
    function rebuild ()
    {
        const source = text();

        if (source === '')
            return;

        const boxes = M.ccall('tw_graph_build', 'number', ['string'],
                              [source]);

        if (boxes <= 0)
        {
            onStatus(`${file} does not parse; the canvas is showing what ` +
                     'last did.');
            return;
        }

        M.ccall('tw_graph_apply_layout', 'number', ['string'], [source]);
        showProbes();

        /* A selection is an index into the boxes, and a rebuild makes new
           ones. Kept only when it still names something. */
        if (selected >= boxes)
            selected = -1;

        M._tw_node_canvas_select(selected);
        view.viewport();
        showParams();
        paint();
    }

    /* Whether the page wants this view at all, which is not the same
     * question as whether its box is open -- composerview.js has the
     * same pair and says why.
     *
     * With the page tiled the box is always open, because the pane's
     * header is its disclosure, and what says whether anybody is looking
     * at it is the layout: a background tab, a collapsed leaf, the mode
     * that is not up. Read as a yes, that is a graph's worth of drawing
     * per animation frame for nobody.
     */
    let wanted = true;

    const refit = () => view.show(wanted && $('nodeview').open);

    const show = (on) =>
    {
        wanted = on;
        refit();
    };

    /* Which .dsp this is over. */
    const showFile = (name) =>
    {
        if (name !== null && name !== file)
        {
            watching?.();
            file = name;

            /* Somebody else's edit to this file -- another peer's, or the
               text editor's on this page -- is a rebuild like our own. */
            watching = files.watch?.(name, rebuild) ?? null;
            selected = -1;
            rebuild();
        }

        refit();
    };

    showPalette();

    $('nodeadd').addEventListener('click', addNode);
    $('nodefile').addEventListener('change',
                                   () => showFile($('nodefile').value));
    $('nodefit').addEventListener('click', () =>
    {
        M._tw_node_canvas_zoom_to_fit();
        paint();
    });
    $('nodeview').addEventListener('toggle', refit);

    /* A menu closes when something else is pressed, which on a canvas is
       most of the time: the next gesture is the answer to it. */
    window.addEventListener('pointerdown', (e) =>
    {
        const menu = $('nodemenu');

        if (!menu.hidden && !menu.contains(e.target))
            menu.hidden = true;
    }, true);

    /* And the browser's own menu stays out of the way: a right-click here
       is asking the canvas, not the page. */
    $('nodecanvas').addEventListener('contextmenu', (e) =>
        e.preventDefault());

    /* The .dsp files in the document, for the selector. */
    const offer = (names) =>
    {
        const select = $('nodefile');
        const was = select.value;

        select.replaceChildren();

        for (const name of names.filter((n) => n.endsWith('.dsp')))
        {
            const option = document.createElement('option');

            option.value = name;
            option.textContent = name;
            select.append(option);
        }

        if (select.options.length === 0)
            return;

        select.value = names.includes(was) ? was : select.options[0].value;
        showFile(select.value);
    };

    /* Where a box is, in the shell pixels a pointer arrives in: the zoom
       applied to what the graph says. The page has no other way to know --
       the layout is the module's -- and a harness that clicked at a guess
       would be testing its guess. */
    const boxAt = (i) =>
    {
        /* Whether anything on it is a plain number somebody could type
           into, which is what makes it worth clicking on for a test and
           for a person. */
        /* Asked of the module rather than worked out here, and asked
           without opening a panel: the page has one open for the selected
           box, and a scan over every box that opened one per box would
           throw it away. */
        const settable = M._tw_graph_box_settable(i) !== 0;

        /* Its ports, where they sit: a port is where a wire starts and
           where a probe is armed, and only the layout knows where one
           is. In shell pixels, like the box. */
        const zoom = M._tw_node_canvas_zoom();
        const ports = [];

        for (let p = 0; p < M._tw_graph_port_count(i); p++)
            ports.push({
                name: call('tw_graph_port_name', ['number', 'number'],
                           [i, p]),
                isInput: M._tw_graph_port_is_input(i, p) !== 0,
                x: (M._tw_graph_box_x(i) + M._tw_graph_port_x(i, p)) * zoom,
                y: (M._tw_graph_box_y(i) + M._tw_graph_port_y(i, p)) * zoom,
            });

        return { name: call('tw_graph_box_name', ['number'], [i]),
                 kind: M._tw_graph_box_kind(i),
                 settable,
                 ports,
                 x: M._tw_graph_box_x(i) * zoom,
                 y: M._tw_graph_box_y(i) * zoom };
    };

    return { offer, show, showFile, rebuild, boxAt, feed,
             /* Whether the frame loop is running: a pane nobody is
                looking at is a graph's worth of drawing for nobody, and
                this is how a harness holds that claim to it. */
             visible: () => view.visible(),
             /* Which channel this instrument is on, for arming a tap. */
             onChannel: (c) => { channel = c; },
             boxes: () => M._tw_graph_box_count(),
             probes: () => probes.length,
             selected: () => selected };
}
