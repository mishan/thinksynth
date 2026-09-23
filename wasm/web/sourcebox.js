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
 * sourcebox.js -- <source-box>, the solo page's code editor.
 *
 * A CodeMirror 6 view behind a textarea's interface: `value' to read and
 * write the text, an `input' event for each edit a person makes and none
 * for a write to `value', and focus() into the text. That is all main.js
 * and the tests ever asked of the textarea this replaces, so they talk to
 * it the same way.
 *
 *   <source-box id="dsp" syntax="dsp" numbered></source-box>
 *
 * `syntax' is dsp or gen (thinklang.js); `numbered' shows line numbers.
 *
 * The solo page loads its modules unbundled, and this is the one with npm
 * imports in it: bundle.mjs builds it into a file of the same name in the
 * build directory, which is what main.js's import finds there.
 */

import { minimalSetup } from 'codemirror';
import { Annotation, Compartment, EditorState,
         Transaction } from '@codemirror/state';
import { EditorView, highlightActiveLine, highlightActiveLineGutter,
         keymap, lineNumbers } from '@codemirror/view';
import { bracketMatching, indentUnit } from '@codemirror/language';
import { highlightSelectionMatches, searchKeymap } from '@codemirror/search';

import { dspLanguage, genLanguage, pageLook } from './thinklang.js';

const LANGUAGES = { dsp: dspLanguage, gen: genLanguage };

/* Marks a change written to `value', which sends no `input' event. */
const fromScript = Annotation.define();

export class SourceBox extends HTMLElement
{
    static observedAttributes = ['numbered'];

    constructor ()
    {
        super();
        this.gutter = new Compartment();
        this.view = null;

        /* The editor's own text and its search field fire the browser's
           `input' events, which would bubble out of the box beside ours:
           two per keystroke, and more for a search typed. Only ours get
           out. This listener is the box's first, so stopping it here
           keeps it from any main.js adds. */
        this.addEventListener('input', (e) =>
        {
            if (e.target !== this)
                e.stopImmediatePropagation();
        });
    }

    connectedCallback ()
    {
        this.made();
    }

    made ()
    {
        if (this.view === null)
            this.view = new EditorView({ parent: this,
                                         state: this.state() });

        return this.view;
    }

    attributeChangedCallback ()
    {
        this.view?.dispatch({
            effects: this.gutter.reconfigure(this.gutterFor()) });
    }

    gutterFor ()
    {
        return this.hasAttribute('numbered')
            ? [lineNumbers(), highlightActiveLineGutter()] : [];
    }

    state ()
    {
        return EditorState.create({
            extensions: [
                minimalSetup,
                this.gutter.of(this.gutterFor()),
                LANGUAGES[this.getAttribute('syntax')] ?? [],
                pageLook,
                indentUnit.of('    '),
                bracketMatching(),
                highlightActiveLine(),
                highlightSelectionMatches(),
                keymap.of(searchKeymap),
                EditorView.contentAttributes.of({
                    spellcheck: 'false', autocorrect: 'off',
                    autocapitalize: 'off' }),
                EditorView.updateListener.of((u) =>
                {
                    if (u.docChanged && !u.transactions.every(
                            (tr) => tr.annotation(fromScript)))
                        this.dispatchEvent(new Event('input',
                                                     { bubbles: true }));
                }),
            ],
        });
    }

    get value ()
    {
        return this.view?.state.doc.toString() ?? '';
    }

    /* Only the stretch that differs is replaced, so the scroll, the
       cursor and a person's undo history outlast a write -- the canvas
       and the popovers write back on every edit they make. The write
       itself is not undoable, as a textarea's is not. */
    set value (text)
    {
        const view = this.made();
        const old = view.state.doc.toString();
        const now = String(text);

        if (now === old)
            return;

        const most = Math.min(old.length, now.length);
        let from = 0;

        while (from < most && old[from] === now[from])
            from++;

        let end = 0;

        while (end < most - from
               && old[old.length - 1 - end] === now[now.length - 1 - end])
            end++;

        view.dispatch({
            changes: { from, to: old.length - end,
                       insert: now.slice(from, now.length - end) },
            annotations: [fromScript.of(true),
                          Transaction.addToHistory.of(false)],
        });
    }

    focus ()
    {
        this.made().focus();
    }
}

customElements.define('source-box', SourceBox);
