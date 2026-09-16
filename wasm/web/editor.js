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
 * editor.js -- the shared document, edited (JAM_M3.md, section 4.2).
 *
 * CodeMirror 6 over the room's Y.Doc through y-codemirror.next: one
 * editor, a tab per file, and everyone's cursors with their names on them.
 * The document is text and the editor is a view of it; the piece and its
 * .dsp files are the files in the map, and a tab appears when a file
 * does. No language mode in M3.
 *
 * This is the first thing on the page with a dependency, and the reason
 * the room page is bundled (bundle.mjs) where the solo page is not.
 */

import { minimalSetup } from 'codemirror';
import { EditorState } from '@codemirror/state';
import { EditorView, keymap, lineNumbers } from '@codemirror/view';
import { indentWithTab } from '@codemirror/commands';
import { yCollab, yUndoManagerKeymap } from 'y-codemirror.next';
import * as Y from 'yjs';

import { fileNames, files } from './doc.js';

/* A colour per peer for the cursor, from the name, so the same person is
   the same colour on every screen. */
export function colourOf (name)
{
    let h = 0;

    for (const ch of name)
        h = (h * 31 + ch.charCodeAt(0)) >>> 0;

    const hue = h % 360;

    return { color: `hsl(${hue} 70% 45%)`, light: `hsl(${hue} 70% 45% / 0.25)` };
}

export class Editor
{
    /* `parent' takes the editor, `tabs' the buttons; `doc' is the room's
       and `awareness' the provider's, with `user' set for the cursors. */
    constructor (parent, tabs, doc, awareness)
    {
        this.parent = parent;
        this.tabs = tabs;
        this.doc = doc;
        this.awareness = awareness;
        this.states = new Map();        /* file name -> EditorState */
        this.current = null;
        this.view = new EditorView({ parent });

        files(doc).observe(() => this.refreshTabs());
        this.refreshTabs();
    }

    /* The state for a file, made on first showing: the text is the
       Y.Text's own, and every edit goes through it. */
    stateFor (name)
    {
        let state = this.states.get(name);

        if (state !== undefined)
            return state;

        const text = files(this.doc).get(name);
        const undo = new Y.UndoManager(text);

        state = EditorState.create({
            doc: text.toString(),
            extensions: [
                minimalSetup,
                lineNumbers(),
                EditorView.lineWrapping,
                keymap.of([...yUndoManagerKeymap, indentWithTab]),
                yCollab(text, this.awareness, { undoManager: undo }),
            ],
        });

        this.states.set(name, state);

        return state;
    }

    show (name)
    {
        if (!files(this.doc).has(name))
            return;

        /* The state left behind is kept as it stands, cursor and all. */
        if (this.current !== null)
            this.states.set(this.current, this.view.state);

        this.current = name;
        this.view.setState(this.stateFor(name));
        this.refreshTabs();
    }

    refreshTabs ()
    {
        const names = fileNames(this.doc);

        this.tabs.replaceChildren();

        for (const name of names)
        {
            const b = document.createElement('button');

            b.textContent = name;
            b.className = name === this.current ? 'tab active' : 'tab';
            b.addEventListener('click', () => this.show(name));
            this.tabs.append(b);
        }

        /* A file that went away takes its state with it; the first file
           is shown when nothing is. */
        for (const name of [...this.states.keys()])
            if (!names.includes(name))
                this.states.delete(name);

        if ((this.current === null || !names.includes(this.current)) &&
            names.length > 0)
            this.show(names.find((n) => n.endsWith('.gen')) ?? names[0]);
    }
}
