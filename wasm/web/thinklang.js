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
 * thinklang.js -- .dsp and .gen for CodeMirror's highlighter.
 *
 * A StreamLanguage over the tokens libthink/thinklex.ll emits, which both
 * languages share: # comments, "strings", numbers, words and punctuation.
 * What differs is which words are keywords, and as in thLexer.h each
 * language keeps its own list. A keyword counts only where a statement
 * starts and is not being assigned to -- `section 8 bars { ... }' is one,
 * `section = 0;' is a knob named section.
 *
 * Beyond the lexer's kinds: `ns::name' is a plugin, `@name' a knob, the
 * word after `.' or `->' a field or port, the word after a declaring
 * keyword the name it declares, and a unit after a number part of it.
 *
 * pageLook, below, is how both pages' editors are drawn.
 */

import { StreamLanguage, syntaxHighlighting } from '@codemirror/language';
import { EditorView } from '@codemirror/view';
import { classHighlighter } from '@lezer/highlight';

const HEADER = ['name', 'author', 'description', 'category'];

/* Keywords after which the next word is the thing being named. */
const DSP_DECLARES = ['node', 'io'];
const GEN_DECLARES = ['instrument', 'chain', 'stage', 'scale', 'section',
                      'preset'];

const DSP_KEYWORDS = new Set([...HEADER, ...DSP_DECLARES]);
const GEN_KEYWORDS = new Set([...HEADER, ...GEN_DECLARES,
                              'seed', 'tempo', 'meter', 'sink', 'input',
                              'effect', 'dsp']);
const DECLARES = new Set([...DSP_DECLARES, ...GEN_DECLARES]);

const UNITS = new Set(['ms', 's', 'b', 'bars', 'beats']);
const ATOMS = new Set(['nil', 'th_max', 'th_min', 'th_range', 'th_midimax',
                       'th_sample']);

const WORD = /^[A-Za-z][A-Za-z0-9_]*/;

function thinkParser (name, keywords)
{
    return {
        name,

        /* `start': at the head of a statement. `after': what the previous
           token was, where that decides what a word is. `depth': brace
           depth, for indentation. */
        startState: () => ({ start: true, after: null, depth: 0 }),

        copyState: (s) => ({ ...s }),

        token (stream, state)
        {
            if (stream.eatSpace())
                return null;

            const after = state.after;
            const start = state.start;

            state.after = null;
            state.start = false;

            const ch = stream.peek();

            if (ch === '#')
            {
                stream.skipToEnd();
                state.start = start;
                state.after = after;
                return 'comment';
            }

            if (ch === '"')
            {
                if (stream.match(/^"[^"]*"/))
                    return 'string';

                stream.skipToEnd();
                return 'invalid';
            }

            if (stream.match(/^[0-9]+(\.[0-9]*)?/))
            {
                state.after = 'number';
                return 'number';
            }

            if (stream.match(/^@[A-Za-z][A-Za-z0-9_]*/))
                return 'variableName.special';

            const word = stream.match(WORD);

            if (word)
            {
                const w = word[0];

                if (stream.match(/^::[A-Za-z][A-Za-z0-9_]*/))
                    return 'typeName';

                if (after === 'number' && UNITS.has(w))
                    return 'number';

                if (after === 'field')
                    return 'propertyName';

                if (after === 'declare')
                    return 'variableName.definition';

                if (ATOMS.has(w))
                    return 'atom';

                const assigned = stream.match(/^\s*=/, false);

                if (start && keywords.has(w) && !assigned)
                {
                    if (DECLARES.has(w))
                        state.after = 'declare';

                    return 'keyword';
                }

                return assigned ? 'propertyName' : null;
            }

            if (stream.match('->') || stream.match('::'))
            {
                state.after = 'field';
                return 'operator';
            }

            stream.next();

            switch (ch)
            {
            case '{':
                state.depth++;
                state.start = true;
                return 'brace';
            case '}':
                state.depth = Math.max(0, state.depth - 1);
                state.start = true;
                return 'brace';
            case ';':
                state.start = true;
                return 'punctuation';
            case '.':
                state.after = 'field';
                return 'punctuation';
            case ',': case '(': case ')':
                return 'punctuation';
            case '=': case '+': case '-': case '*': case '/': case '%':
            case '$':
                return 'operator';
            default:
                return 'invalid';
            }
        },

        indent (state, textAfter, cx)
        {
            const close = /^\s*}/.test(textAfter) ? 1 : 0;

            return Math.max(0, state.depth - close) * cx.unit;
        },

        languageData: { commentTokens: { line: '#' } },
    };
}

export const dspLanguage = StreamLanguage.define(
    thinkParser('dsp', DSP_KEYWORDS));
export const genLanguage = StreamLanguage.define(
    thinkParser('gen', GEN_KEYWORDS));

/* By file name, as the room's tabs have them; null for anything else. */
export function languageFor (name)
{
    if (name.endsWith('.dsp'))
        return dspLanguage;

    if (name.endsWith('.gen'))
        return genLanguage;

    return null;
}

/* CodeMirror's base theme is a light one. This is the page's colors in
   either scheme, as style.css has them, and the tokens as classes so the
   syntax colors are style.css's too (the tok- rules there). */
const MIX = (color, pct) =>
    `color-mix(in srgb, ${color} ${pct}%, transparent)`;

const SELECTION = ['&.cm-focused > .cm-scroller > .cm-selectionLayer ' +
                   '.cm-selectionBackground',
                   '.cm-selectionBackground',
                   '.cm-content ::selection'].join(', ');

export const pageLook = [
    syntaxHighlighting(classHighlighter),
    EditorView.theme({
        '&': { color: 'var(--fg)', backgroundColor: 'var(--panel)' },
        '.cm-content': { caretColor: 'var(--fg)' },
        '.cm-cursor, .cm-dropCursor': { borderLeftColor: 'var(--fg)' },
        [SELECTION]: { backgroundColor: MIX('AccentColor', 30) },
        '.cm-activeLine, .cm-activeLineGutter':
            { backgroundColor: MIX('var(--fg)', 5) },
        '.cm-selectionMatch': { backgroundColor: MIX('AccentColor', 15) },
        '&.cm-focused .cm-matchingBracket':
            { backgroundColor: MIX('AccentColor', 25) },
        '.cm-gutters': {
            backgroundColor: 'var(--panel)',
            color: 'var(--dim)',
            borderRight: '1px solid var(--line)',
        },
        '.cm-panels': { backgroundColor: 'var(--panel)', color: 'var(--fg)' },
        '.cm-panels.cm-panels-bottom': { borderTop: '1px solid var(--line)' },
    }),
];
