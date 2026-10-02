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
 * editor.js -- the shared document, edited.
 *
 * CodeMirror 6 over the room's Y.Doc through y-codemirror.next: one
 * editor, a tab per file, and everyone's cursors with their names on them.
 * The document is text and the editor is a view of it; the piece and its
 * .dsp files are the files in the map, and a tab is a file the piece names
 * (doc.js, pieceFiles): it appears when the .gen names a file the map
 * has, and goes when the .gen stops naming it. Each tab is highlighted as
 * its file's language (thinklang.js).
 *
 * This is the first thing on the page with a dependency, and the reason
 * the room page is bundled whole (bundle.mjs) where the solo page bundles
 * only its source boxes (sourcebox.js).
 */

import { minimalSetup } from 'codemirror';
import { EditorState } from '@codemirror/state';
import { EditorView, keymap, lineNumbers } from '@codemirror/view';
import { indentWithTab } from '@codemirror/commands';
import { yCollab, yUndoManagerKeymap } from 'y-codemirror.next';
import * as Y from 'yjs';

import { files, meta, pieceFiles } from './doc.js';
import { languageFor, pageLook } from './thinklang.js';

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
        /* file name -> { text, undo, state }: the Y.Text a state was made
           over, and its undo history. */
        this.states = new Map();
        this.current = null;
        this.shown = null;              /* the tabs as last drawn */
        this.view = new EditorView({ parent });

        /* Deep, because which files are tabs is in the .gen's text. */
        files(doc).observeDeep(() => this.refreshTabs());
        meta(doc).observe(() => this.refreshTabs());
        this.refreshTabs();
    }

    /* The state for a file. Kept from its last showing only while it is
       over the text the name holds now and says what that text says:
       y-codemirror follows a text only while its view is showing it, so
       one changed behind its back -- a peer's edit, or a switch putting a
       new text under the name -- would take keystrokes at offsets that no
       longer hold. */
    stateFor (name)
    {
        const text = files(this.doc).get(name);
        const kept = this.states.get(name);

        if (kept !== undefined && kept.text === text &&
            kept.state.doc.toString() === text.toString())
            return kept.state;

        this.forget(name);

        const undo = new Y.UndoManager(text);
        const state = EditorState.create({
            doc: text.toString(),
            extensions: [
                minimalSetup,
                lineNumbers(),
                EditorView.lineWrapping,
                languageFor(name) ?? [],
                pageLook,
                keymap.of([...yUndoManagerKeymap, indentWithTab]),
                yCollab(text, this.awareness, { undoManager: undo }),
            ],
        });

        this.states.set(name, { text, undo, state });

        return state;
    }

    forget (name)
    {
        this.states.get(name)?.undo.destroy();
        this.states.delete(name);
    }

    show (name)
    {
        if (!files(this.doc).has(name))
            return;

        /* The state left behind is kept as it stands, cursor and all. */
        const left = this.states.get(this.current);

        if (left !== undefined)
            left.state = this.view.state;

        this.current = name;
        this.view.setState(this.stateFor(name));
        this.refreshTabs();
    }

    refreshTabs ()
    {
        const map = files(this.doc);
        const names = pieceFiles(this.doc);

        /* The file shown stays a tab for as long as the document has it,
           named or not: typing a .gen through a line that names it would
           otherwise throw whoever is in it back to the .gen. */
        if (map.has(this.current) && !names.includes(this.current))
        {
            names.push(this.current);
            names.sort();
        }

        /* And shown again when a new text is put under its name. */
        const rebind = map.has(this.current) &&
                       this.states.get(this.current)?.text !==
                       map.get(this.current);
        const shown = JSON.stringify([names, this.current]);

        /* Called on every keystroke in every file. */
        if (shown === this.shown && !rebind)
            return;

        this.shown = shown;
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
            if (!map.has(name))
                this.forget(name);

        if (rebind)
            this.show(this.current);
        else if ((this.current === null || !names.includes(this.current)) &&
                 names.length > 0)
            this.show(names.find((n) => n.endsWith('.gen')) ?? names[0]);
    }
}
