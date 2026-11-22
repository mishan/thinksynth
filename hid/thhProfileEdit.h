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

#ifndef THH_PROFILE_EDIT_H
#define THH_PROFILE_EDIT_H

#include <string>

#include "thHidBlock.h"

/* Edits to a player's profile, the text of their changes to a device's
   own (docs/HID.md, "Your own profile"), as the configurator makes them
   (docs/PHONE.md): each a splice of the statement it changes, so the
   player's comments and layout survive, as thcGenEdit's are of a .gen.

   Each edits the block for either hand, `device NAME { ... };', making it
   if there is none. A layer's bindings, and a block for one hand, are left
   to the text. Each answers false, with `err', and leaves the text as it
   was, when it doesn't read as a profile: no splice is safe in text whose
   statements aren't known. */

/* Whether a binding of `kind' takes `option' (channel, min, max, at, off,
   step, scale, range, center, smooth, shape, both, latch), as the parser
   has it (Parser::options). */
bool thhTakes (thHidBinding::Kind kind, const std::string &option);

/* A binding as the profile language writes it, `control = action
   { options };', with the options it has the parser wouldn't assume. */
std::string thhBindingText (const thHidBinding &b);

/* The player's `which'th binding of `b.control' made `b', or `b' added
   when there is no `which'th. */
bool thhProfileSet (std::string &text, const std::string &device,
                    const thHidBinding &b, int which, std::string &err);

/* The player's `which'th binding of `control' taken out. */
bool thhProfileRemove (std::string &text, const std::string &device,
                       const std::string &control, int which,
                       std::string &err);

/* Every binding of the player's of `control', and any ignore of it, taken
   out: the device's own bindings of it are what play. */
bool thhProfileReset (std::string &text, const std::string &device,
                      const std::string &control, std::string &err);

/* Reset, and `ignore control;' added: the device's own bindings of it
   outside its layers are dropped too. */
bool thhProfileOff (std::string &text, const std::string &device,
                    const std::string &control, std::string &err);

#endif /* THH_PROFILE_EDIT_H */
