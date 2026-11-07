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
 */

#ifndef THC_FREEZE_H
#define THC_FREEZE_H

#include <string>
#include <utility>
#include <vector>

#include "thcGenEdit.h"

class thcScheduler;

/* Freezing a chain: what it just played, as a pattern that plays it
 * back.
 *
 * A generative chain plays something good and moves on; freezing keeps
 * it. The last whole bars the chain was heard to play (thcChain::played)
 * are quantized onto a step grid and written as the params of a
 * gen::grid -- a ladder of the pitches it used, a row per pitch, a cell
 * per step, ties for notes longer than a step, accents for the louder
 * ones -- which is a stage a person can then click and edit like any
 * other grid.
 *
 * In beats where the piece writes beats: sixteenth steps, whole bars of
 * the piece's meter ending at the last bar line. A piece in seconds has
 * no bar, and gets eighth-of-a-second steps over the last four seconds.
 * Either way it is at most 64 steps, the grid's own limit: fewer bars
 * where the meter is long, and a bar of more than sixteen beats is cut
 * to its last sixty-four sixteenths. The window is measured at the
 * tempo now, so a tempo change inside it misplaces what came before.
 *
 * A held note -- live input, with no duration until its release -- is
 * as long as the release made it (thcScheduler records the off).
 */
class thcFreeze
{
public:
    typedef std::vector<std::pair<std::string, std::string> > Params;

    /* The params of a gen::grid playing back the last `bars' bars chain
       `chain' was heard to play, spelled as a file writes them. False,
       with `why', when it played nothing in them. */
    static bool fromChain (const thcScheduler &sched, size_t chain, int bars,
                           Params &out, std::string &why);

    /* The frozen chain `name', written into the .gen at `path' beside
       `from': the grid `params', through `from's first note sink, with
       its `start' and its level in every section of the arrangement
       -- the running piece's, lane edits and all -- so it plays where,
       when and as loud as the chain it was frozen from. */
    static thcGenEdit::Result write (const std::string &path,
                                     const thcGenEdit::Chain &from,
                                     const thcScheduler &sched,
                                     const std::string &name,
                                     const Params &params,
                                     std::string &why);

    /* `name_frozen', or `name_frozen2' and on where that is taken. */
    static std::string frozenName (const std::string &name,
                                   const std::vector<std::string> &taken);
};

#endif /* THC_FREEZE_H */
