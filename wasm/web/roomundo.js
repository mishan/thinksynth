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
    const e = { text, um, last: null, join: null, at: 0, what: TYPED,
                refused: null };

    /* An undo or redo makes a new item on the other stack, and the name
       goes with it. */
    um.on('stack-item-added', ({ stackItem, origin }) =>
    {
        const from = origin === um ? um.currStackItem?.meta : null;

        stackItem.meta.set('what', from?.get('what') ??
                                   (origin === GRAPH ? e.what : TYPED));

        /* A new edit: the step refused last is no longer what an Undo
           would come to first. */
        if (origin !== um)
            e.refused = null;
    });

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

/* The history the node view takes (nodeview.js, createNodeView), and the
   code view's keys. `parses' says whether a text builds a graph, or null
   while it cannot yet tell. */
export function roomHistory (doc, parses = () => true)
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

        /* The step taken, and what to say of it when there is more than
           its name: { what, said }, or null with none to take.

           A step that would turn a text that parses into one that does not
           is put straight back and said. Taken back over others' edits
           since, a step can do that -- theirs depending on it, or the
           splices between having mixed whose characters are whose, which
           no later edit need ever undo. So the same step asked for again,
           and refused again, is dropped, its edit left in, and the one
           before it taken: no step can stand in front of the others for
           good. */
        go (name, back)
        {
            const e = entryFor(doc, name);

            if (e === null)
                return null;

            const stack = back ? e.um.undoStack : e.um.redoStack;
            const verb = back ? 'undo' : 'redo';
            const before = e.text.toString();
            const parsed = parses(before);

            if (parsed === null)
                return stack.length === 0 ? null
                    : { what: nameOf(stack.at(-1)),
                        said: `${back ? 'Undo' : 'Redo'} waits a moment: ` +
                              'what parses is not known yet.' };

            const item = back ? e.um.undo() : e.um.redo();
            const again = e.refused?.back === back && e.refused.item === item;

            e.refused = null;

            if (item === null)
                return null;

            const what = nameOf(item);

            if (!parsed || parses(e.text.toString()))
                return { what };

            if (back)
                e.um.redo();
            else
                e.um.undo();

            if (!again)
            {
                e.refused = { back, item: stack.at(-1) };

                return { what, said: `Can't ${verb} ${what}: with the edits ` +
                                     'since, the patch would not parse. ' +
                                     `${back ? 'Undo' : 'Redo'} again to leave ` +
                                     'it as it is and go on.' };
            }

            stack.pop();
            heardOf(doc, name).forEach((cb) => cb());

            const next = this.go(name, back);
            const left = `Left ${what} as it is`;

            return next === null
                ? { what, said: `${left}; nothing before it to ${verb}.` }
                : { what: next.what,
                    said: next.said ?? `${left}, and ${verb === 'undo'
                        ? 'undid' : 'redid'} ${next.what}.` };
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
