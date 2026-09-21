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

#ifndef KNOB_PANEL_H
#define KNOB_PANEL_H 1

/*
 * A piece's knobs, as a thPanel.
 *
 * The smallest of the providers (see PanelModel.h), and the one that shows
 * what the split between describing an edit and delivering it is for.
 *
 * A knob is heard. Moving one has to land at the same transport time on
 * every peer or the peers stop composing the same piece, so a knob move in
 * the browser is a stamped command -- out to the room, back in at its time,
 * applied by every instance including the one that made it. On the desktop
 * there is one instance and no room, and the delivery is a thArg::setValue.
 * Same rows, same ranges, same spelling of every number; two deliveries.
 *
 * So what leaves here is the description and the intent, and deliver() is
 * the desktop's half of the fork. The browser's half is tw_knob, which was
 * already there: a command carrying a transport time, whose handler makes
 * the same write deliver() makes. Nothing about the panel decides which --
 * that is the shell's, which is the whole point of the intent being a
 * separate thing from the write.
 *
 * A knob's row id is its index, spelled out. A command names a knob by the
 * number the module gave it, so that is what a row has to carry; the name
 * is the tooltip and the label is what is shown. Hidden knobs keep their
 * numbers and get no row -- the numbering is the module's, over every knob
 * the piece declared, and a row list that renumbered them would send a
 * command to the wrong knob.
 */

#include <string>
#include <vector>

#include "PanelModel.h"

class thArg;

class KnobPanel
{
public:
    KnobPanel (void);

    /* The knobs, in the order a command numbers them.
     *
     * Held, not copied: a knob is a thArg owned by the scheduler, and this
     * is as stale as whatever the caller is holding -- loading a piece frees
     * every one of them, and both the caller's list and this one are rebuilt
     * from the load. ArgPanel looks its args up by name for the same hazard;
     * here there is nothing to look them up in, because a knob's index is
     * assigned by whoever collected the list. */
    void setKnobs (const std::vector<thArg *> &knobs);

    /* The panel. False -- and an empty panel -- for a piece with no knobs
       anyone may move. */
    bool build (thPanel &out) const;

    /* What an edit of `row' to `valueText' means. Writes nothing; fills
       `out' with a KNOB intent whose `a' is the index a command names the
       knob by. */
    thPanelResult propose (const string &row, const string &valueText,
                           thPanelEdit &out) const;

    /* The desktop's delivery, and the far end of the browser's command.
       False when the knob has gone, which is what a stale intent looks
       like. */
    bool deliver (const thPanelEdit &edit) const;

    /* What a row's control should be showing, from the knob as it stands
       now. False when there is none. */
    bool valueFor (const string &row, double &display) const;

    /* The knob a row stands for, so a shell can subscribe to it, and NULL
       for a row that is not there. */
    thArg *argFor (const string &row) const;

private:
    std::vector<thArg *> knobs_;
};

#endif /* KNOB_PANEL_H */
