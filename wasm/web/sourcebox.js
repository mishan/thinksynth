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
import { Compartment, EditorState } from '@codemirror/state';
import { EditorView, highlightActiveLine, highlightActiveLineGutter,
         keymap, lineNumbers } from '@codemirror/view';
import { bracketMatching, indentUnit } from '@codemirror/language';
import { highlightSelectionMatches, searchKeymap } from '@codemirror/search';

import { dspLanguage, genLanguage, pageLook } from './thinklang.js';

const LANGUAGES = { dsp: dspLanguage, gen: genLanguage };

export class SourceBox extends HTMLElement
{
    static observedAttributes = ['numbered'];

    constructor ()
    {
        super();
        this.gutter = new Compartment();
        this.view = null;
    }

    connectedCallback ()
    {
        this.made();
    }

    made ()
    {
        if (this.view === null)
            this.view = new EditorView({ parent: this,
                                         state: this.stateFor('') });

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

    /* A fresh state, as a textarea's value written from script starts
       over: no undo past it, and the cursor at the top. */
    stateFor (text)
    {
        return EditorState.create({
            doc: text,
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
                    if (u.docChanged)
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

    set value (text)
    {
        this.made().setState(this.stateFor(String(text)));
    }

    focus ()
    {
        this.made().focus();
    }
}

customElements.define('source-box', SourceBox);
