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

#include "thhHands.h"

static int
side (const std::string &hand)
{
    return hand == "left" ? 0 : hand == "right" ? 1 : -1;
}

void
thhAssignHands (std::vector<thhHandSlot> &slots)
{
    static const char *const NAME[2] = { "left", "right" };
    bool taken[2] = { false, false };
    std::vector<bool> given(slots.size(), false);

    /* Those that say, first come first. */
    for (size_t i = 0; i < slots.size(); i++)
    {
        const int h = side(slots[i].said);

        if (slots[i].connected && h >= 0 && !taken[h])
        {
            slots[i].hand = NAME[h];
            taken[h] = given[i] = true;
        }
    }

    /* Then those the player has said, first come first. */
    for (size_t i = 0; i < slots.size(); i++)
    {
        const int h = side(slots[i].remembered);

        if (slots[i].connected && !given[i] && h >= 0 && !taken[h])
        {
            slots[i].hand = NAME[h];
            taken[h] = given[i] = true;
        }
    }

    /* Then the rest: the hand each had, if it is free, the right, the
       left, or, a third, none. */
    for (size_t i = 0; i < slots.size(); i++)
    {
        if (!slots[i].connected || given[i])
            continue;

        int h = side(slots[i].hand);

        if (h < 0 || taken[h])
            h = !taken[1] ? 1 : !taken[0] ? 0 : -1;
        slots[i].hand = h >= 0 ? NAME[h] : "";
        if (h >= 0)
            taken[h] = true;
    }
}
