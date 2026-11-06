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

#ifndef GTH_HID_H
#define GTH_HID_H

#include <functional>
#include <memory>
#include <string>

#include <glibmm/dispatcher.h>

#include "thhMapper.h"
#include "thhReader.h"

class thSynth;

/* A wired Index controller, played in patch mode: read on its own thread,
   mapped by the player's profile (~/.config/thinksynth/hid/index, or the
   controller's own), and played on the GUI thread the way the on-screen
   keyboard plays, so the roll and a piece's Kbd input hear it too.

   Linux only. Made after the main window and gone before it. */
class gthHid
{
public:
    /* channel is the keyboard's, 0-based, for bindings that name none. */
    gthHid (thSynth *synth, const std::function<int ()> &channel);
    ~gthHid ();

private:
    thSynth              *synth_;
    std::function<int ()> channel_;
    thhMapper             mapper_;
    std::string           said_;

    /* Before the reader, so it outlives the reader's thread, which emits
       it until it is joined. */
    Glib::Dispatcher           dispatcher_;
    std::unique_ptr<thhReader> reader_;

    void drained ();
};

#endif /* GTH_HID_H */
