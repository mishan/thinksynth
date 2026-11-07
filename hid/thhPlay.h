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

#ifndef THH_PLAY_H
#define THH_PLAY_H

#include <functional>
#include <string>
#include <vector>

#include "think.h"
#include "thhMapper.h"

/* Plays a mapper's actions on a synth as the on-screen keyboard plays keys:
   notes through addNote and delNote, and values written straight onto the
   channel's arg, in the arg's own range where the profile gave none. A
   value for an arg the channel's patch hasn't got is dropped. heard, if
   set, is told of each note played. On the thread the GUI runs on. */
void thhPlay (thSynth &synth, const std::vector<thhAction> &acts,
              const std::function<void (const thhAction &)> &heard);

/* What a hand ("left" or "right") plays: the device's own profile
   (fallback), its blocks for either hand and then for this one, with the
   player's file at path on top, its either-hand block and then this
   hand's. False, with the file, line and what is wrong in err, if the
   file is there and doesn't fit the device; out is then the device's own
   for that hand. */
bool thhLoadProfile (const std::string &path, const thhControls &controls,
                     const char *fallback, const std::string &hand,
                     thcHidBlock &out, std::string &err);

#endif /* THH_PLAY_H */
