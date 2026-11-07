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

#ifndef THH_HANDS_H
#define THH_HANDS_H

#include <string>
#include <vector>

/* A controller as hand-giving sees it: whether it is connected, the hand
   it says it is (or empty), and the hand it plays. */
struct thhHandSlot
{
    bool        connected;
    std::string said, hand;
};

/* Gives each connected controller a hand, "left" or "right": the one it
   says, unless another connected one has it already; one that doesn't
   say, or can't have the one it says, keeps the hand it had if that is
   free and takes the free one if not. A controller not connected keeps
   its hand for when it is back, and holds no hand from the rest. */
void thhAssignHands (std::vector<thhHandSlot> &slots);

#endif /* THH_HANDS_H */
