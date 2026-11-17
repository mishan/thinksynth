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
 * On the solo page it is the patch in the source box, or one of the pieces'
 * instruments, and the write reloads what is playing. The canvas, the
 * graph and every edit are the same on both.
 *
 * The forms are HTML for the same reason they are gtkmm on the desktop: a
 * form is the platform's. What is in them -- which args a node has, what a
 * plugin's ports are, why an edit was refused -- comes from the model, on
 * both.
 */

import { createCanvasView, modsOf } from './canvasview.js';
import { placePopover } from './popover.js';
import { numberIn, showPanel } from './panel.js';

/* NodeEdit::Result::OK, and the signal kinds thinknode.cpp queues. */
const OK = 0;

/* thPanel::Kind, for the panel over the selected box. */
const NODE_VALUE = 3;

/* What tw_node_propose says an edit of a node panel's row is:
   thPanelEdit::Kind, or text typed back as it stands. */
const EDIT_VALUE = 3, EDIT_INFO = 4, EDIT_RENAME = 5, EDIT_EXPRESSION = 6;
const ECHO = -2;

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
    NUDGED: 9,
    DELETE: 10,
    WIRE_SELECTED: 11,
    WIRE_CONTEXT: 12,
};

/* CanvasContent::Key, by a keydown's key, and Ctrl+A by its key code, so
   it is the A key on any layout. */
const KEYS = { Escape: 1, ArrowLeft: 2, ArrowRight: 3, ArrowUp: 4,
               ArrowDown: 5, Delete: 6, Backspace: 6 };
const KEY_SELECT_ALL = 7;

const keyOf = ({ key, code, mods }) => (mods & 2) && code === 'KeyA'
    ? KEY_SELECT_ALL : KEYS[key] ?? 0;


/* NodeGraph::Box kinds, as tw_graph_box_kind reports them. */
const BOX = { NODE: 0, CONTROL: 1, IO_IN: 2, IO_OUT: 3, PROBE: 4 };

export async function createNodeView ({ files, root = document,
                                        onStatus = () => {},
                                        probe = null, unprobe = null,
                                        sampleRate = 48000 })
{
    const $ = (id) => root.getElementById(id);

    /* What this view says goes in its own line as well as the page's: the
       page's is the reload's a moment after an edit. */
    const say = (text) =>
    {
        const line = $('nodesay');

        if (line !== null)
            line.textContent = text;
    };

    const pageStatus = onStatus;

    onStatus = (text) =>
    {
        pageStatus(text);
        say(text);
    };

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

    /* The positions back into the file's own layout block, which is where
       the desktop keeps them. */
    const writeLayout = () =>
    {
        if (M.ccall('tw_layout_write', 'number', ['string'], [text()]))
            files.write(file, M.UTF8ToString(M._tw_edit_text()));
    };

    /* Boxes moved by keys and not written yet. A rebuild drops them: the
       graph they were moved in is gone, and writing its positions into the
       text that replaced it would undo whatever that text changed. */
    let nudged = false;

    const writeNudged = () =>
    {
        if (!nudged)
            return;

        nudged = false;

        /* A drag under way writes them with its own move, and a write now
           would rebuild the graph out from under it. */
        if (!M._tw_node_canvas_busy())
            writeLayout();
    };

    /* Delete on the canvas: everything selected that can go, as the
       desktop's does -- not the io node, and not an expression, which is
       an arg's text. One write for the lot, so a room gets one splice and
       a patch one reload: each removal is spliced into the text the last
       one left. */
    function deleteSelection ()
    {
        const victims = [];
        let ioStays = false, exprStays = false;

        for (let i = 0; i < M._tw_node_canvas_selection_count(); i++)
        {
            const b = M._tw_node_canvas_selection_at(i);
            const kind = M._tw_graph_box_kind(b);

            if ((kind === BOX.NODE && !isExpr(b)) || kind === BOX.CONTROL)
                victims.push(kind === BOX.CONTROL
                    ? { control: call('tw_graph_box_control', ['number'],
                                      [b]) }
                    : { node: call('tw_graph_box_name', ['number'], [b]) });
            else if (kind === BOX.IO_IN || kind === BOX.IO_OUT)
                ioStays = true;
            else
                exprStays = true;
        }

        const stays = ioStays && exprStays
            ? '  The io node and expressions stay.'
            : ioStays ? '  The io node stays.'
            : exprStays ? '  Expressions stay; cutting a wire into one ' +
                          'removes it.'
            : '';

        if (victims.length === 0)
        {
            if (stays)
                onStatus(`Nothing to delete.${stays}`);

            return;
        }

        let next = text();

        for (const v of victims)
        {
            const r = v.control !== undefined
                ? M.ccall('tw_edit_remove_control', 'number',
                          ['string', 'string'], [next, v.control])
                : M.ccall('tw_edit_remove_node', 'number',
                          ['string', 'string'], [next, v.node]);

            if (r !== OK)
            {
                onStatus(`${call('tw_edit_result_text', ['number'], [r])}: ` +
                         M.UTF8ToString(M._tw_edit_why()));
                return;
            }

            next = M.UTF8ToString(M._tw_edit_text());
        }

        /* What watched them goes first: a panel would have no host. */
        const gone = new Set(victims.map((v) => v.node ?? `@${v.control}`));

        for (let i = probes.length - 1; i >= 0; i--)
            if (gone.has(probes[i].node))
                stopProbe(i);

        selected = -1;
        files.write(file, next);
        onStatus(victims.length === 1
            ? `Deleted ${v0(victims[0])}.${stays}`
            : `Deleted ${victims.length} nodes.${stays}`);
    }

    const v0 = (v) => v.node ?? `@${v.control}`;

    /* ---- the canvas ---- */

    const view = createCanvasView({
        scroller: $('nodescroll'),
        canvas: $('nodecanvas'),
        send: (m) => toModule(m),
        takes: (e) => keyOf({ key: e.key, code: e.code,
                              mods: modsOf(e) }) !== 0,

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
                                        m.nPress ?? 1, m.mods ?? 0);
                break;

            case 'motion':
                M._tw_node_canvas_motion(m.x, m.y);
                break;

            case 'release':
                M._tw_node_canvas_release(m.x, m.y, m.button ?? 1);
                break;

            case 'key':
            {
                const took = M._tw_node_canvas_key(keyOf(m),
                                                   m.mods ?? 0) !== 0;

                drain();

                return took;
            }

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

                    /* One of a group is shown, and the group is what was
                       chosen: SELECTION says that. */
                    if (a >= 0 && M._tw_node_canvas_selection_count() <= 1)
                        say(`${call('tw_graph_box_name', ['number'], [a])}, ` +
                            call('tw_graph_box_plugin', ['number'], [a]));
                    break;

                case SIG.SELECTION:
                    if (a > 1)
                        say(`${a} nodes selected.`);
                    break;

                /* Written when the key comes up: a held arrow repeats,
                   and every write reloads a patch or goes to every peer. */
                case SIG.NUDGED:
                    nudged = true;
                    paint();
                    break;

                case SIG.DELETE:
                    deleteSelection();
                    break;

                case SIG.WIRE_SELECTED:
                    if (a >= 0)
                        onStatus(`The wire ${wireName(a)}.  Delete cuts it.`);
                    break;

                case SIG.WIRE_CONTEXT:
                    offerWire(a, sig.x, sig.y);
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
                    writeLayout();
                    break;
            }
        }

        offerDelete();
    }

    /* The toolbar's Delete, for whatever is selected: there is nothing to
       take without a selection. */
    function offerDelete ()
    {
        $('nodedelete').disabled = M._tw_node_canvas_selection_count() === 0;
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

        cut(call('tw_graph_box_name', ['number'], [box]),
            call('tw_graph_port_name', ['number', 'number'], [box, port]));
    }

    function cut (node, arg)
    {
        if (edit('tw_edit_disconnect',
                 ['string', 'string', 'string', 'number'], [node, arg, 0]))
            onStatus(`Disconnected ${node}.${arg}.`);
    }

    const end = (box, port) =>
        `${call('tw_graph_box_name', ['number'], [box])}.` +
        call('tw_graph_port_name', ['number', 'number'], [box, port]);

    /* `from.out -> to.in', as the desktop's status bar and menu name one. */
    const wireName = (e) =>
        `${end(M._tw_graph_edge_from_box(e), M._tw_graph_edge_from_port(e))}` +
        ` -> ${end(M._tw_graph_edge_to_box(e), M._tw_graph_edge_to_port(e))}`;

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
    /* The menu's parts: an item, a title, and showing it beside the
       pointer -- or hiding it, with nothing in it to offer. */
    function menuAt (x, y)
    {
        const menu = $('nodemenu');

        menu.replaceChildren();

        /* Closed by an item or Escape, it gives the focus back to the
           canvas it was asked for on, not to the page. */
        const close = () =>
        {
            menu.hidden = true;
            $('nodecanvas').focus({ preventScroll: true });
        };

        const item = (label, go) =>
        {
            const b = document.createElement('button');

            b.textContent = label;
            b.addEventListener('click', () =>
            {
                close();
                go();
            });
            menu.append(b);
        };

        const title = (text) =>
        {
            const t = document.createElement('span');

            t.className = 'menutitle';
            t.textContent = text;
            menu.ariaLabel = text;
            menu.append(t);
        };

        menu.onkeydown = (e) =>
        {
            if (e.key !== 'Escape')
                return;

            close();
            e.preventDefault();
            e.stopPropagation();
        };

        /* Tabbed out of, it is done with, and not left open behind. */
        menu.onfocusout = (e) =>
        {
            if (!menu.contains(e.relatedTarget))
                menu.hidden = true;
        };

        /* Beside the pointer, in the page's own coordinates: the canvas
           said where in its own pixels, and the element says where it
           is. Held inside the window by placePopover, since a pane can
           be narrower than this menu is. */
        const show = () =>
        {
            if (!menu.querySelector('button'))
            {
                menu.hidden = true;
                return;
            }

            const at = $('nodecanvas').getBoundingClientRect();

            placePopover(menu, at.left + window.scrollX + x,
                         at.top + window.scrollY + y);
            menu.querySelector('button')?.focus();
        };

        return { item, title, show };
    }

    function offerWire (edge, x, y)
    {
        if (edge < 0)
            return;

        /* By name, resolved when clicked: a peer's edit in between
           renumbers the wires. */
        const { item, title, show } = menuAt(x, y);
        const to = end(M._tw_graph_edge_to_box(edge),
                       M._tw_graph_edge_to_port(edge));

        title(wireName(edge));
        item('Disconnect', () =>
        {
            const dot = to.lastIndexOf('.');

            cut(to.slice(0, dot), to.slice(dot + 1));
        });
        show();
    }

    function offerProbe (box, port, x, y)
    {
        if (box < 0)
        {
            $('nodemenu').hidden = true;
            return;
        }

        const { item, title, show } = menuAt(x, y);
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

            if (probe !== null && channel < 0)
                item('nothing is playing this instrument', () => {});
            else if (probe !== null && already >= 0)
                item('Stop watching', () => stopProbe(already));
            else if (probe !== null)
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

            title(node);

            for (const i of mine)
                item(`Stop watching ${probes[i].node}.${probes[i].arg}`,
                     () => stopProbe(i));
        }

        /* The box itself, as Delete would take it: the selection when the
           box is in it, else the box alone -- a right-click does not
           select, and taking whatever was would take the wrong thing. */
        const kind = M._tw_graph_box_kind(box);

        if (kind === BOX.CONTROL)
        {
            const ctl = call('tw_graph_box_control', ['number'], [box]);

            item(`Name, range, label and group of @${ctl}\u2026`,
                 () => editControl(ctl));
        }

        if ((kind === BOX.NODE && !isExpr(box)) || kind === BOX.CONTROL)
            item(`Delete ${node}`, () =>
            {
                for (let b = 0; b < M._tw_graph_box_count(); b++)
                    if (call('tw_graph_box_name', ['number'], [b]) === node &&
                        M._tw_graph_box_kind(b) === kind)
                    {
                        let held = false;

                        for (let i = 0;
                             i < M._tw_node_canvas_selection_count(); i++)
                            held ||= M._tw_node_canvas_selection_at(i) === b;

                        if (!held)
                            M._tw_node_canvas_select(b);

                        drain();
                        deleteSelection();
                        return;
                    }
            });

        show();
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

        /* Where the view is, beside a box already there, and selected --
           its parameters the ones showing, as on the desktop. */
        if (edit('tw_edit_add_node', ['string', 'string', 'string'],
                 [name, plugin]))
        {
            M.ccall('tw_graph_place', 'number', ['string'], [name]);
            drain();
            writeLayout();
            onStatus(`Added ${name} (${plugin}).`);
        }
    }

    /* ---- a control's form ----
     *
     * The desktop's New control and right-click dialogs, as one <dialog>: a
     * name, a value when adding, a range, a label and a group. The answer is
     * tried against the text before anything is written, and a refusal is
     * said in the form, which stays open to be put right -- a name the lexer
     * would not read back, one another control has, a quote in a label. */
    function controlForm (setup, apply)
    {
        $('nodecontrol')?.remove();

        /* For this file and no other: in a room the one shown can change
           under an open form, and that closes it (showFile). */
        const on = file;

        const dlg = document.createElement('dialog');
        const form = document.createElement('form');
        const heading = document.createElement('h3');
        const why = document.createElement('p');
        const hint = document.createElement('p');
        const fields = {};

        dlg.id = 'nodecontrol';
        dlg.ariaLabel = setup.title;
        heading.textContent = setup.title;
        form.append(heading);

        const field = (key, text, type, value, placeholder = '') =>
        {
            const label = document.createElement('label');
            const input = document.createElement('input');

            input.type = type;
            input.name = key;
            input.value = value;
            input.placeholder = placeholder;

            if (type === 'number')
                input.step = 'any';

            label.append(`${text} `, input);
            form.append(label);
            fields[key] = input;
        };

        field('name', 'Name', 'text', setup.name);

        if (setup.value !== undefined)
            field('value', 'Value', 'number', setup.value);

        field('min', 'Minimum', 'number', setup.min);
        field('max', 'Maximum', 'number', setup.max);
        field('label', 'Label', 'text', setup.label, 'optional');
        field('group', 'Group', 'text', setup.group, 'optional');

        hint.textContent = 'The name is what nodes read as @name.  Sliders ' +
                           'sharing a group are drawn as one titled block.';
        why.setAttribute('role', 'alert');

        const cancel = document.createElement('button');
        const ok = document.createElement('button');

        cancel.type = 'button';
        cancel.textContent = 'Cancel';
        cancel.onclick = () => dlg.close();
        ok.type = 'submit';
        ok.textContent = setup.ok;
        form.append(hint, why, cancel, ok);

        form.onsubmit = (e) =>
        {
            e.preventDefault();

            const got = {
                name: fields.name.value.trim(),
                value: fields.value ? numberIn(fields.value.value) : 0,
                min: numberIn(fields.min.value),
                max: numberIn(fields.max.value),
                label: fields.label.value,
                group: fields.group.value,
            };

            if (file !== on)
                why.textContent = `This was for ${on}, which is not the ` +
                                  'file shown now.';
            else if ([got.value, got.min, got.max].some((n) => n === null))
                why.textContent = 'Each of the numbers wants a number.';
            else if (!(got.max > got.min))
                why.textContent = 'The maximum must be above the minimum.';
            else
            {
                const refused = apply(got);

                if (refused === null)
                    dlg.close();
                else
                    why.textContent = refused;
            }
        };

        dlg.onclose = () =>
        {
            dlg.remove();
            $('nodecanvas').focus();
        };

        dlg.append(form);
        (root.body ?? document.body).append(dlg);
        dlg.showModal();
        fields.name.select();
    }

    /* Edits in a row over one text, written once: null, or the sentence
       the first refused one came with. */
    function chain (steps)
    {
        let next = text();

        for (const [name, types, args] of steps)
        {
            const r = M.ccall(name, 'number', ['string', ...types],
                              [next, ...args]);

            if (r !== OK)
                return `${call('tw_edit_result_text', ['number'], [r])}: ` +
                       M.UTF8ToString(M._tw_edit_why());

            next = M.UTF8ToString(M._tw_edit_text());
        }

        files.write(file, next);
        return null;
    }

    const boxNamed = (name) =>
    {
        for (let b = 0; b < M._tw_graph_box_count(); b++)
            if (call('tw_graph_box_name', ['number'], [b]) === name)
                return b;

        return -1;
    };

    const META = ['string', 'number', 'number', 'string', 'string'];

    function addControl ()
    {
        if (file === null)
            return;

        /* A name nothing else uses, offered as a starting point, as the
           palette names a node: ctl, then ctl2. */
        const taken = new Set();

        for (let b = 0; b < M._tw_graph_box_count(); b++)
            if (M._tw_graph_box_kind(b) === BOX.CONTROL)
                taken.add(call('tw_graph_box_control', ['number'], [b]));

        let name = 'ctl';

        for (let n = 2; taken.has(name); n++)
            name = `ctl${n}`;

        controlForm({ title: 'New control', ok: 'Add', name, value: 0.5,
                      min: 0, max: 1, label: '', group: '' }, (c) =>
        {
            const refused = chain([['tw_edit_add_control',
                                    ['string', 'number', ...META.slice(1)],
                                    [c.name, c.value, c.min, c.max, c.label,
                                     c.group]]]);

            if (refused === null)
            {
                M._tw_node_canvas_select(boxNamed(`@${c.name}`));
                drain();
                onStatus(`Added @${c.name}.  Drag from its output to a ` +
                         'parameter to use it.');
            }

            return refused;
        });
    }

    /* A control's name, range, label and group, from its right-click. The
       range first and the name last, so the text written is the rename's,
       which is what carries the selection across. */
    function editControl (was)
    {
        const box = boxNamed(`@${was}`);

        if (box < 0)
            return;

        const meta = (b) => ({
            min: M._tw_graph_box_control_min(b),
            max: M._tw_graph_box_control_max(b),
            label: call('tw_graph_box_control_label', ['number'], [b]),
            group: call('tw_graph_box_control_group', ['number'], [b]),
        });
        const opened = meta(box);

        controlForm({ title: `Control @${was}`, ok: 'Apply', name: was,
                      ...opened }, (c) =>
        {
            /* What is there now, for each field not typed into: in a room
               a peer may have changed one since the form opened, and
               writing the form's copy back would undo them. */
            const at = boxNamed(`@${was}`);

            if (at < 0)
                return `@${was} is not in the patch any more.`;

            const now = meta(at);
            const pick = (k) => (c[k] === opened[k] ? now[k] : c[k]);
            const steps = [['tw_edit_set_control_meta', META,
                            [was, pick('min'), pick('max'), pick('label'),
                             pick('group')]]];

            if (c.name !== was)
                steps.push(['tw_edit_rename', ['number', 'string', 'string'],
                            [1, was, c.name]]);

            const watched = probes.filter((p) => p.node === `@${was}`);

            for (const p of watched)
                p.node = `@${c.name}`;

            const refused = chain(steps);

            if (refused !== null)
                for (const p of watched)
                    p.node = `@${was}`;
            else
                onStatus(`@${c.name} now runs ${pick('min')} to ` +
                         `${pick('max')}.`);

            return refused;
        });
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
     * for one: what the panel says it means -- a value, a header field, a
     * name or arithmetic -- is a rewrite of the `.dsp', and the new text goes
     * to whoever owns the file. See commit().
     */
    function showParams ()
    {
        outline();

        const box = $('nodeparams');

        box.replaceChildren();

        /* Nothing selected is the patch's header, once there is a patch. */
        const node = selected < 0
            ? '' : call('tw_graph_box_name', ['number'], [selected]);

        if (text() === '' || M._tw_panel_open(NODE_VALUE, selected, 0) === 0)
        {
            $('nodeselected').textContent = node;
            return;
        }

        const panel = JSON.parse(M.UTF8ToString(M._tw_panel_json()));

        $('nodeselected').textContent = `${panel.title} — ${panel.subtitle}`;

        const at = selected;

        showPanel(box, panel, (row, typed) => commit(at, row, typed, node));
    }

    /* What an edit of a panel's row means is the panel's to say -- the one
     * the desktop draws (src/NodePanel.cpp) -- and this splices it.
     *
     * By the box the panel was drawn for, which is the one `node' names.
     * A refusal is a panel catching up or a box emptied, and goes unsaid, as
     * it does on the desktop; a splice refused is said by edit(). Either way
     * the panel is put back to what the file says. */
    function commit (box, row, typed, node)
    {
        const kind = M.ccall('tw_node_propose', 'number',
                             ['number', 'string', 'string', 'string'],
                             [box, row, typed, text()]);
        const target = M.UTF8ToString(M._tw_node_intent_node());
        const arg = M.UTF8ToString(M._tw_node_intent_row());
        let done = false;

        if (kind === EDIT_VALUE)
            done = edit('tw_edit_set_value',
                        ['string', 'string', 'string', 'number'],
                        [node, arg, M._tw_node_intent_value()]);
        else if (kind === EDIT_INFO)
            done = edit('tw_edit_set_info', ['string', 'string', 'string'],
                        [arg, typed]);
        else if (kind === EDIT_RENAME)
            done = rename(box, typed);
        else if (kind === EDIT_EXPRESSION)
            done = edit('tw_edit_set_expression',
                        ['string', 'string', 'string', 'string'],
                        [target, arg, typed]);
        else if (kind === ECHO)
            return;

        if (!done)
            showParams();
    }

    /* A node or a control called `to': every reference, its layout and its
       probes. The build that follows keeps it selected. */
    function rename (box, to)
    {
        const control = M._tw_graph_box_kind(box) === 1;
        const at = control ? '@' : '';
        const from = control
            ? call('tw_graph_box_control', ['number'], [box])
            : call('tw_graph_box_name', ['number'], [box]);

        /* Before the edit, whose write rebuilds and puts the panels back. */
        for (const p of probes)
            if (p.node === at + from)
                p.node = at + to;

        if (!edit('tw_edit_rename', ['string', 'number', 'string', 'string'],
                  [control ? 1 : 0, from, to]))
        {
            for (const p of probes)
                if (p.node === at + to)
                    p.node = at + from;

            return false;
        }

        onStatus(`Renamed ${at}${from} to ${at}${to}.`);
        return true;
    }

    /* ---- the outline ----
     *
     * The graph for a keyboard and a screen reader: every box, to choose
     * as a click on it chooses (and so to edit in the panel), and the
     * chosen one's wires -- each input and what feeds it, to cut; a new
     * one from any output, refused for the reason the canvas would give
     * (tw_graph_can_connect); an output to watch; and the node itself to
     * delete. Each is the call the canvas's gesture makes.
     */
    /* What a box is called in the list: its name and what it is, which is
       its plugin, the io halves' included, or a control. */
    const named = (b) =>
    {
        const what = call('tw_graph_box_plugin', ['number'], [b]) ||
                     (M._tw_graph_box_kind(b) === BOX.CONTROL ? 'control'
                                                              : '');

        return call('tw_graph_box_name', ['number'], [b]) +
               (what && ` (${what})`);
    };

    /* An expression's box (`osc.freq = ...'): its inputs are its text's,
       and it is a node's arg rather than a node to delete. */
    const isExpr = (b) =>
        call('tw_graph_box_plugin', ['number'], [b]) === 'expression';

    const portName = (b, p) =>
        call('tw_graph_port_name', ['number', 'number'], [b, p]);

    const el = (tag, props = {}, ...kids) =>
    {
        const e = Object.assign(document.createElement(tag), props);

        e.append(...kids);
        return e;
    };

    /* Chosen from the list: the canvas's selection and the panel, as a
       click, and the box brought into view. */
    function choose (b)
    {
        selected = b;
        M._tw_node_canvas_select(b);
        offerDelete();

        const zoom = M._tw_node_canvas_zoom();

        $('nodescroll').scrollTo({
            left: Math.max(0, M._tw_graph_box_x(b) * zoom - 40),
            top: Math.max(0, M._tw_graph_box_y(b) * zoom - 40) });
        showParams();
        paint();
    }

    $('nodeoutline').addEventListener('toggle', () => outline());

    function outline ()
    {
        /* Nothing to keep up to date while it is shut: it is made again
           when it opens. */
        if (!$('nodeoutline').open)
            return;

        const list = $('nodelist');
        const count = file === null ? 0 : M._tw_graph_box_count();
        const shown = [];

        for (let b = 0; b < count; b++)
            if (M._tw_graph_box_kind(b) !== BOX.PROBE)
                shown.push(b);

        /* The same boxes as last drawn keep their buttons, so the one a
           person is on keeps the focus; only which is chosen moves. */
        const names = shown.map(named);

        if (names.join('\n') !== list.dataset.names)
        {
            list.dataset.names = names.join('\n');
            list.replaceChildren(...shown.map((b, i) =>
                el('li', {}, el('button', { type: 'button',
                                            textContent: names[i],
                                            onclick: () => choose(b) }))));
        }

        [...list.children].forEach((li, i) =>
        {
            const on = shown[i] === selected;

            li.firstChild.ariaCurrent = on ? 'true' : null;
        });

        wiring();
    }

    function wiring ()
    {
        const box = $('nodewiring');
        const b = selected;

        if (!$('nodeoutline').open)
            return;

        if (file === null || b < 0 || M._tw_graph_box_kind(b) === BOX.PROBE)
        {
            box.replaceChildren();
            box.dataset.made = '';
            return;
        }

        const name = call('tw_graph_box_name', ['number'], [b]);
        const ports = M._tw_graph_port_count(b);
        const edges = [];

        for (let e = 0; e < M._tw_graph_edge_count(); e++)
            edges.push({ e, fromBox: M._tw_graph_edge_from_box(e),
                         fromPort: M._tw_graph_edge_from_port(e),
                         toBox: M._tw_graph_edge_to_box(e),
                         toPort: M._tw_graph_edge_to_port(e) });

        const ins = [];
        const outs = [];

        for (let p = 0; p < ports; p++)
            (M._tw_graph_port_is_input(b, p) ? ins : outs).push(p);

        /* Made again only when what it shows has changed: an edit
           elsewhere -- a peer's, a keystroke in the source -- would
           otherwise take the focus and a half-made choice out of a menu
           somebody is in. */
        const made = JSON.stringify([
            named(b), channel, probes.map((p) => `${p.node}.${p.arg}`),
            edges.filter((x) => x.toBox === b).map((x) =>
                [x.toPort, call('tw_graph_box_name', ['number'],
                                [x.fromBox]), x.fromPort]),
            M._tw_graph_box_count()]);

        if (made === box.dataset.made)
            return;

        box.dataset.made = made;
        const expr = isExpr(b);

        /* After an action the panel is made again; the focus goes back to
           the chosen box in the list rather than to the page. */
        const home = () => [...$('nodelist').querySelectorAll('button')]
            .find((x) => x.ariaCurrent === 'true')?.focus();

        const rows = ins.map((p) =>
        {
            const from = edges.find((x) => x.toBox === b && x.toPort === p);
            const li = el('li', {}, `${portName(b, p)} from ` +
                (from === undefined ? 'nothing'
                    : `${call('tw_graph_box_name', ['number'],
                              [from.fromBox])}.` +
                      portName(from.fromBox, from.fromPort)));

            if (from !== undefined && !expr)
                li.append(' ', el('button', {
                    type: 'button', textContent: 'Disconnect',
                    ariaLabel: `Disconnect ${name}.${portName(b, p)}`,
                    onclick: () => { disconnect(from.e); home(); } }));

            return li;
        });

        const parts = [el('h3', { textContent: `${name}'s wires` }),
                       el('ul', { ariaLabel: `${name}'s inputs` }, ...rows)];

        /* A wire into one of its inputs, from any output of another box. */
        if (ins.length > 0 && !expr)
        {
            const to = el('select', { ariaLabel: 'Input' },
                          ...ins.map((p) => new Option(portName(b, p), p)));
            const from = el('select', { ariaLabel: 'From' });

            for (let o = 0; o < M._tw_graph_box_count(); o++)
                for (let p = 0; o !== b && p < M._tw_graph_port_count(o); p++)
                    if (!M._tw_graph_port_is_input(o, p))
                        from.append(new Option(
                            `${call('tw_graph_box_name', ['number'], [o])}.` +
                            portName(o, p), `${o}:${p}`));

            parts.push(el('div', { className: 'row' },
                el('label', {}, 'Connect ', to),
                el('label', {}, ' from ', from),
                el('button', { type: 'button', textContent: 'Connect',
                    onclick: () =>
                    {
                        const [o, p] = from.value.split(':').map(Number);
                        const t = Number(to.value);

                        if (M._tw_graph_can_connect(o, p, b, t) === 0)
                            onStatus(`No wire: ${M.UTF8ToString(
                                M._tw_graph_why())}`);
                        else
                        {
                            connect(o, p, b, t);
                            home();
                        }
                    } })));
        }

        /* An output to watch, as the probe menu offers, and the ones being
           watched to stop. */
        if (probe !== null && outs.length > 0 && M._tw_visual_count() > 0)
        {
            const port = el('select', { ariaLabel: 'Output' },
                            ...outs.map((p) => new Option(portName(b, p), p)));
            const visual = el('select', { ariaLabel: 'With' });

            for (let v = 0; v < M._tw_visual_count(); v++)
            {
                const n = call('tw_visual_name', ['number'], [v]);

                visual.append(new Option(n, n));
            }

            parts.push(el('div', { className: 'row' },
                el('label', {}, 'Watch ', port),
                el('label', {}, ' with a ', visual),
                el('button', { type: 'button', textContent: 'Watch',
                    disabled: channel < 0,
                    title: channel < 0 ? 'Nothing is playing this ' +
                                         'instrument' : '',
                    onclick: async () =>
                    {
                        const arg = portName(b, Number(port.value));

                        /* One watch to a port, as the probe menu has it. */
                        if (probes.some((p) => p.node === name &&
                                               p.arg === arg))
                            onStatus(`Already watching ${name}.${arg}.`);
                        else
                            await armProbe(name, arg, visual.value);

                        home();
                    } })));
        }

        probes.forEach((p, i) =>
        {
            if (p.node === name)
                parts.push(el('button', {
                    type: 'button',
                    textContent: `Stop watching ${p.node}.${p.arg}`,
                    onclick: () => { stopProbe(i); home(); } }));
        });

        /* The box itself, but for the io halves, which are the patch's
           ends rather than a node in it. */
        const kind = M._tw_graph_box_kind(b);

        if ((kind === BOX.NODE && !expr) || kind === BOX.CONTROL)
            parts.push(el('button', {
                type: 'button', textContent: `Delete ${name}`,
                onclick: () =>
                {
                    /* What watched it goes first: its panel would have no
                       host, and nothing left to stop it from. And nothing
                       is chosen before the write, which rebuilds at once:
                       the index would otherwise name the box after it. */
                    for (let i = probes.length - 1; i >= 0; i--)
                        if (probes[i].node === name)
                            stopProbe(i);

                    const was = selected;

                    selected = -1;

                    const gone = kind === BOX.CONTROL
                        ? edit('tw_edit_remove_control',
                               ['string', 'string'],
                               [call('tw_graph_box_control', ['number'],
                                     [b])])
                        : edit('tw_edit_remove_node', ['string', 'string'],
                               [name]);

                    if (gone)
                    {
                        onStatus(`Deleted ${name}.`);
                        $('nodelist').querySelector('button')?.focus();
                    }
                    else
                        selected = was;
                } }));

        box.replaceChildren(...parts);
    }

    /* ---- the file ---- */

    /* Built, laid out, and drawn. Called on opening a file and whenever
       the document's text for it changes -- which is both this page's
       edits and everybody else's. */
    function rebuild ()
    {
        nudged = false;

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

        /* The build kept the selection by name. */
        selected = M._tw_node_canvas_selected();
        view.viewport();
        showParams();
        offerDelete();
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

    /* And whether there is a file to draw: with none (offer, below) the
       canvas is hidden, and drawing into it would be for nobody too. */
    let hasFile = true;

    const refit = () => view.show(wanted && hasFile && $('nodeview').open);

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
            $('nodecontrol')?.close();

            /* Somebody else's edit to this file -- another peer's, or the
               text editor's on this page -- is a rebuild like our own. */
            watching = files.watch?.(name, rebuild) ?? null;

            /* Or the build would keep a box of the same name selected, in
               a file where nobody chose it, one key from Delete. */
            selected = -1;
            M._tw_node_canvas_select(-1);
            rebuild();
        }

        refit();
    };

    showPalette();

    $('nodeadd').addEventListener('click', addNode);
    $('nodeaddcontrol').addEventListener('click', addControl);
    $('nodefile').addEventListener('change',
                                   () => showFile($('nodefile').value));
    $('nodefit').addEventListener('click', () =>
    {
        M._tw_node_canvas_zoom_to_fit();
        paint();
    });

    /* The desktop's steps. */
    const zoomTo = (z) =>
    {
        M._tw_node_canvas_set_zoom(z);
        paint();
    };

    $('nodezoomin').addEventListener('click',
        () => zoomTo(M._tw_node_canvas_zoom() * 1.25));
    $('nodezoomout').addEventListener('click',
        () => zoomTo(M._tw_node_canvas_zoom() / 1.25));
    $('nodezoom1').addEventListener('click', () => zoomTo(1));

    /* Hand placement discarded, so it is an edit: the layout block is
       written again from the arranged graph. */
    $('nodearrange').addEventListener('click', () =>
    {
        if (file === null)
            return;

        M._tw_graph_arrange();
        writeLayout();
        onStatus('Arranged.');
    });

    $('nodedelete').addEventListener('click', () =>
    {
        deleteSelection();
        offerDelete();
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

    $('nodecanvas').addEventListener('keyup', writeNudged);
    $('nodecanvas').addEventListener('blur', writeNudged);

    /* Undo and Save as the desktop's graph has them, said rather than left
       to the page: its Ctrl+Z undoes the piece, and the browser's Ctrl+S
       saves the page. */
    $('nodecanvas').addEventListener('keydown', (e) =>
    {
        if (!(e.ctrlKey || e.metaKey) ||
            !['KeyZ', 'KeyY', 'KeyS'].includes(e.code))
            return;

        e.preventDefault();
        e.stopPropagation();
        onStatus(e.code === 'KeyS'
            ? 'The graph has nothing to save: its edits are in the patch ' +
              'as they are made.'
            : 'No Undo in the browser\'s graph yet.');
    });

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

        /* Nothing to edit is said, rather than shown as a menu with
           nothing in it over a canvas with nothing on it. Only where the
           page has the words for it: the room page has no #nodeempty,
           and its graph is whatever the room is playing. */
        const none = select.options.length === 0;
        const empty = $('nodeempty');

        /* And the file it was over let go of: a selection and its panel
           left up would still take edits, and an edit writes the file. */
        if (none && file !== null)
        {
            watching?.();
            watching = null;
            file = null;
            nudged = false;
            selected = -1;
            M._tw_node_canvas_select(-1);
            showParams();
        }

        hasFile = !none;
        refit();

        if (empty !== null)
        {
            select.closest('.row').hidden = none;
            $('nodescroll').hidden = none;
            empty.hidden = !none;
        }

        if (none)
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
             onChannel: (c) =>
             {
                 channel = c;
                 wiring();
             },
             boxes: () => M._tw_graph_box_count(),
             probes: () => probes.length,
             zoom: () => M._tw_node_canvas_zoom(),
             selected: () => selected };
}
