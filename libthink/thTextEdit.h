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

#ifndef TH_TEXT_EDIT_H
#define TH_TEXT_EDIT_H

#include <string>
#include <vector>

#include "thExport.h"

/* Edits to source text by the byte spans its tokens carry (thWords.h), so
   everything between them -- comments, indentation, blank lines -- is left
   as the author wrote it: thcGenEdit's for a .gen, thhProfileEdit's for a
   controller profile. */

struct thTextEdit
{
    size_t a, b;         /* replace [a, b)                              */
    std::string repl;
};

/* Every edit, or none: false, and the text untouched, if any two overlap. */
THINK_API bool thApplyEdits (std::string &text, std::vector<thTextEdit> &edits);

/* Where the line holding `pos' starts. */
THINK_API size_t thLineStart (const std::string &text, size_t pos);

/* Where a statement appended to a block ends up, the block's `}' at
   `bodyClose', and whether it gets a line of its own. */
THINK_API size_t thEndOfBlock (const std::string &text, size_t bodyClose,
                               bool &ownLine);

/* A space before `at', unless one is there already. */
THINK_API std::string thGapBefore (const std::string &text, size_t at);

/* The edit that removes the statement at [a, b), and its line if it had one
   to itself. */
THINK_API thTextEdit thEraseStmt (const std::string &text, size_t a, size_t b);

#endif /* TH_TEXT_EDIT_H */
