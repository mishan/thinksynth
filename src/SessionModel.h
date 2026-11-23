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
 * SessionModel.h -- a piece's form as a grid: a row per chain, a column per
 * section in play order, and in each cell the chain's level there; rows too
 * for each knob's value per section. What the session grid on the page and
 * in the desktop's composer draws, with no toolkit in it, read from the
 * piece's text and edited through thcGenEdit, by name throughout.
 */

#ifndef SESSIONMODEL_H
#define SESSIONMODEL_H

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "thcGenEdit.h"

struct thSession
{
    /* A chain's level, or a knob's value, as the section writes it; not
       written, a chain plays at 1 and a knob stays where it was. */
    struct Cell
    {
        bool written = false;
        double value = 1;
    };

    /* A chain, or a knob (`knob'). `notes' for a chain that plays notes
       rather than writing a knob or a chanarg; `cues' for one that writes
       the knob a `cue' follows, which a section muting it stops. */
    struct Row
    {
        std::string name;
        bool knob = false;
        bool notes = false;
        bool cues = false;
        std::vector<Cell> cells;
    };

    struct Column
    {
        std::string name;
        std::string length;         /* as written: `16 bars' */
        bool hold = false;
        bool playing = false;
        bool cued = false;
    };

    enum Ending { CYCLES, ENDS, LOOPS };

    std::vector<Column> columns;
    std::vector<Row> rows;
    Ending ending = CYCLES;
    std::string loopTo;
    std::string cueKnob;            /* the knob a `cue' follows, or "" */
};

/* One thing done to the grid. `section' is the column it is done to, or
   the one an added section follows ("" for last); `row' a chain's name or
   a knob's with its `@', as a knob and a chain may share a name. */
struct thSessionEdit
{
    enum Kind
    {
        LEVEL,          /* row's cell: `value', or for a knob `set' false
                           to leave it where it was */
        ADD,            /* `name', `length' (1 bars if ""),
                           after `section'                    */
        DUPLICATE,      /* `section' again as `name'          */
        REMOVE,
        RENAME,         /* to `name'                          */
        LENGTH,         /* to `length'                        */
        HOLD,           /* `set'                              */
        MOVE,           /* after `after', first for ""        */
        END,            /* `set': the list ends, or goes round */
        LOOP,           /* the list goes round to `section',
                           or from the first for ""          */
    };

    Kind kind = LEVEL;
    std::string section, row, name, length, after;
    double value = 1;
    bool set = true;
};

class SessionModel
{
public:
    /* The grid of `filename', with `playing' and `cued' the scheduler's
       section indices, -1 for none. */
    bool build (const std::string &filename, int playing, int cued,
                thSession &out, std::string &why);

    /* Whether `edit' may be made to the grid last built, and why not; and
       anything worth saying before it is (`warn'). A piece whose sections
       a cue knob names by number is not reordered, added to or taken from:
       the knob's values would mean other sections. */
    bool propose (const thSessionEdit &edit, std::string &why,
                  std::string &warn) const;

    /* `edit' made to `filename', as the file's own text. */
    thcGenEdit::Result apply (const std::string &filename,
                              const thSessionEdit &edit, std::string &why);

    /* What a click on a chain's cell does: off, or back to the level it
       had before it was turned off, remembered by name across builds. */
    thSessionEdit toggle (const std::string &section,
                          const std::string &chain);

private:
    const thSession::Row *row (const std::string &name) const;
    int column (const std::string &name) const;

    thSession last_;
    std::map<std::pair<std::string, std::string>, double> remembered_;
};

#endif
