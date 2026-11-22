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

#include <algorithm>

#include "thTextEdit.h"

/* Applied back to front, so earlier offsets stay true while later text
 * moves. Edits are built against one index over one read, so they can
 * never overlap unless an operation is buggy -- and a buggy operation
 * must not half-happen: overlaps are detected before the first byte
 * moves, and false means the text was not touched at all. A partially
 * applied edit set is a file nobody wrote.
 *
 * stable_sort ascending, then applied in reverse. The stability and
 * the direction together make same-offset inserts land in push order:
 * the later-pushed insert is applied first and the earlier one then
 * lands above it, so what setKnobMeta pushes as min, max, label reads
 * back as min, max, label -- on every platform, every run. Plain sort
 * left equal offsets to the implementation's mood. */
bool
thApplyEdits (std::string &text, std::vector<thTextEdit> &edits)
{
    std::stable_sort(edits.begin(), edits.end(),
                     [](const thTextEdit &x, const thTextEdit &y)
                     { return x.a < y.a; });

    for (size_t i = 0; i + 1 < edits.size(); i++)
        if (edits[i].b > edits[i + 1].a)
            return false;

    for (size_t i = edits.size(); i-- > 0; )
        text.replace(edits[i].a, edits[i].b - edits[i].a, edits[i].repl);

    return true;
}

size_t
thLineStart (const std::string &text, size_t pos)
{
    size_t nl = text.rfind('\n', pos ? pos - 1 : 0);

    return nl == std::string::npos ? 0 : nl + 1;
}

/* Where a statement appended to a block goes, and whether it gets a line
 * of its own.
 *
 * `bodyClose' is the block's `}'. When everything before it on its line is
 * blank the block is written across several lines and the statement goes
 * above the brace, indented. When something shares that line the block is
 * a one-liner -- `chain c { stage s gen::eno_line { }; };' -- and the
 * start of the line is *before* the statement the block belongs to.
 * Inserting there puts the new statement above its own block at top
 * level, where the loader reads it as a statement it does not know and
 * the file no longer loads. So a one-liner stays a one-liner and the text
 * goes just inside the brace instead.
 */
size_t
thEndOfBlock (const std::string &text, size_t bodyClose, bool &ownLine)
{
    const size_t ls = thLineStart(text, bodyClose);

    ownLine = text.find_first_not_of(" \t", ls) >= bodyClose;

    return ownLine ? ls : bodyClose;
}

/* A space of our own only where there is not one already, so `{ a = 1; }'
   and `{ a = 1;}' both come out readable. */
std::string
thGapBefore (const std::string &text, size_t at)
{
    return (at > 0 && (text[at - 1] == ' ' || text[at - 1] == '\t'))
        ? "" : " ";
}

/* Remove a statement and, when it sat on a line of its own, the line --
 * indentation and newline included. When something else shares the line
 * (another statement, or a trailing comment worth keeping), only the
 * statement's own bytes and the blanks after it go: what stays keeps the
 * line's indentation. */
thTextEdit
thEraseStmt (const std::string &text, size_t a, size_t b)
{
    const size_t ls = thLineStart(text, a);
    const bool lineBefore = text.find_first_not_of(" \t", ls) >= a;

    size_t e = b;

    while (e < text.size() && (text[e] == ' ' || text[e] == '\t'))
        e++;

    bool lineAfter = e >= text.size() || text[e] == '\n';

    thTextEdit ed;

    if (lineBefore && lineAfter)
    {
        ed.a = ls;
        ed.b = e < text.size() ? e + 1 : e;   /* the newline too         */
    }
    else
    {
        ed.a = a;
        ed.b = e;
    }

    return ed;
}
