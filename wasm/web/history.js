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
 * history.js -- the texts a document has been, for Undo and Redo.
 *
 * A step is a text and the name of what made it. A run of edits that say
 * they are one -- the same `join', each less than a second after the last, as a
 * slider dragged through the values of one arg -- is one step back to
 * where the first began, under the first one's name.
 */

export class TextHistory
{
    constructor ({ keep = 200, joinMs = 1000,
                   now = () => performance.now() } = {})
    {
        this.keep = keep;
        this.joinMs = joinMs;
        this.now = now;
        this.clear();
    }

    /* Another document: nothing before it to go back to. The next text
       recorded is where it starts. */
    clear ()
    {
        this.text = null;
        this.past = [];
        this.future = [];
        this.join = null;
        this.at = 0;
    }

    /* The document is `text' now, made by an edit called `what'. */
    record (text, what = '', join = null)
    {
        if (text === this.text)
            return;

        const now = this.now();

        if (this.text !== null &&
            (join === null || join !== this.join ||
             now - this.at >= this.joinMs || this.past.length === 0))
        {
            this.past.push({ text: this.text, what });
            this.past.splice(0, this.past.length - this.keep);
        }

        this.text = text;
        this.future = [];
        this.join = join;
        this.at = now;
    }

    get canUndo () { return this.past.length > 0; }
    get canRedo () { return this.future.length > 0; }
    get undoName () { return this.past.at(-1)?.what ?? ''; }
    get redoName () { return this.future.at(-1)?.what ?? ''; }

    /* The text to show, and the name of the step taken; null with nothing
       to take. */
    undo () { return this.#step(this.past, this.future); }
    redo () { return this.#step(this.future, this.past); }

    #step (from, to)
    {
        const s = from.pop();

        if (s === undefined)
            return null;

        to.push({ text: this.text, what: s.what });
        this.text = s.text;
        this.join = null;

        return { text: s.text, what: s.what };
    }
}
