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
 * roomundo.js -- a room's Undo: one history per file, for both its views.
 *
 * A Y.UndoManager over the file's Y.Text, tracking this page's typing in
 * the code view (yCollab adds its own origin while it shows the file) and
 * its edits on the graph, written with GRAPH. A peer's edit arrives with
 * the provider as its origin and is never a step, and neither is what the
 * page writes for a knob or a pick; an Undo takes back one's own step,
 * rebased over whatever else changed since.
 *
 * A step is one gesture on the graph, under the name the desktop gives it,
 * or a run of typing. Values set on one arg less than a second apart join,
 * as they do on the desktop; typing and the graph never join each other.
 */

import * as Y from 'yjs';

import { files } from './doc.js';

/* The origin the graph writes with. */
export const GRAPH = Symbol('graph');

const JOIN_MS = 1000;
const TYPED = 'edit text';

/* doc -> { byName: name -> { text, um, last, join, at, what },
            heard: name -> what to tell when its stacks change } */
const rooms = new WeakMap();

function roomOf (doc)
{
    let room = rooms.get(doc);

    if (room === undefined)
    {
        const byName = new Map();

        rooms.set(doc, room = { byName, heard: new Map() });

        /* Typing after a graph step is a step of its own: its first
           transaction ends the graph's, which would otherwise take it in. */
        doc.on('beforeTransaction', (tr) =>
        {
            for (const e of byName.values())
                if (tr.origin !== GRAPH && tr.origin !== e.um &&
                    e.um.trackedOrigins.has(tr.origin))
                {
                    if (e.last === GRAPH)
                        e.um.stopCapturing();

                    e.last = null;
                }
        });
    }

    return room;
}

/* Kept by name, not by manager: a switch puts a new text, and so a new
   manager, under the name a view is listening to. */
function heardOf (doc, name)
{
    const { heard } = roomOf(doc);

    if (!heard.has(name))
        heard.set(name, new Set());

    return heard.get(name);
}

function entryFor (doc, name)
{
    const { byName } = roomOf(doc);
    const text = files(doc).get(name);
    const kept = byName.get(name);

    if (kept?.text === text)
        return kept;

    /* A new text under the name, or none: nothing before it to go back
       to. */
    kept?.um.destroy();
    byName.delete(name);

    if (text === undefined)
        return null;

    const um = new Y.UndoManager(text, { trackedOrigins: new Set([GRAPH]),
                                         captureTimeout: JOIN_MS });
    const e = { text, um, last: null, join: null, at: 0, what: TYPED };

    /* An undo or redo makes a new item on the other stack, and the name
       goes with it. */
    um.on('stack-item-added', ({ stackItem, origin }) =>
        stackItem.meta.set('what',
            origin === um ? um.currStackItem?.meta.get('what') ?? TYPED
            : origin === GRAPH ? e.what : TYPED));

    for (const event of ['stack-item-added', 'stack-item-popped',
                         'stack-item-updated'])
        um.on(event, () => heardOf(doc, name).forEach((cb) => cb()));

    byName.set(name, e);

    return e;
}

/* The file's manager, for the code view's yCollab. */
export function undoFor (doc, name)
{
    return entryFor(doc, name)?.um ?? null;
}

const nameOf = (item) => (item === undefined || item === null ? null
                          : item.meta.get('what') ?? TYPED);

/* The history the node view takes (nodeview.js, createNodeView). */
export function roomHistory (doc)
{
    return {
        step (name, what, join, fn)
        {
            const e = entryFor(doc, name);

            if (e === null)
                return fn();

            const now = Date.now();

            if (e.last !== GRAPH || join === null || join !== e.join ||
                now - e.at >= JOIN_MS)
                e.um.stopCapturing();

            e.what = what;

            const done = fn();

            Object.assign(e, { last: GRAPH, join, at: now });

            return done;
        },

        names (name)
        {
            const um = entryFor(doc, name)?.um;

            return { undo: nameOf(um?.undoStack.at(-1)),
                     redo: nameOf(um?.redoStack.at(-1)) };
        },

        go (name, back)
        {
            const um = entryFor(doc, name)?.um;

            return nameOf(back ? um?.undo() : um?.redo());
        },

        /* The stacks change after a text's observers have run, so a view
           that redraws from those hears of it again here. */
        onChange (name, cb)
        {
            const heard = heardOf(doc, name);

            heard.add(cb);

            return () => heard.delete(cb);
        },
    };
}
