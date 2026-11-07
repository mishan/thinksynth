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
   value for an arg the channel's patch hasn't got is dropped, and so is a
   loop, which is the host's to keep. heard, if set, is told of each note
   played. On the thread the GUI runs on. */
void thhPlay (thSynth &synth, const std::vector<thhAction> &acts,
              const std::function<void (const thhAction &)> &heard);

/* What a hand ("left" or "right") plays: the device's own profile
   (fallback), its blocks for either hand and then for this one; over that
   the player's file at path, its either-hand block and then this hand's;
   and over that a patch's controller blocks (patch, a profile's text, as
   thSynthTree::controller() gives it), the same way. The patch defines
   the controller, and the player's file fills what it leaves. False, with
   the file, line and what is wrong in err, if the file or the patch
   doesn't fit the device; out then plays without whichever doesn't, and
   patchErr, if given, says what is wrong with the patch's, or nothing if
   it plays. */
bool thhLoadProfile (const std::string &path, const thhControls &controls,
                     const char *fallback, const std::string &hand,
                     thcHidBlock &out, std::string &err,
                     const std::string &patch = "",
                     const std::string &patchName = "",
                     std::string *patchErr = NULL);

#endif /* THH_PLAY_H */
