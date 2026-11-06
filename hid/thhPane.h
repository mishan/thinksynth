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

#ifndef THH_PANE_H
#define THH_PANE_H

#include <functional>
#include <string>
#include <vector>

#include "thcHidBlock.h"
#include "thhControls.h"

/* What the controller pane shows, without a toolkit: a row per binding,
   the profile's own first and then each layer's. */
struct thhPaneRow
{
    std::string layer;      /* empty for the profile's own */
    std::string control, action, target;
    std::string value;      /* the control now: down, up, or where it is */
    std::string warning;    /* what won't work, or empty */
};

/* curChannel is the keyboard's, 0-based; hasArg says whether a channel's
   patch has a knob of that name. */
std::vector<thhPaneRow>
thhPaneRows (const thcHidBlock &profile, const thhControls &controls,
             const thhState &now, int curChannel,
             const std::function<bool (int, const std::string &)> &hasArg);

/* What the pane reads, on the GUI thread. */
class thhPaneSource
{
public:
    virtual ~thhPaneSource () {}

    virtual const thcHidBlock &profile () const = 0;
    virtual unsigned generation () const = 0;   /* bumped on each reload */
    virtual thhState state () const = 0;
    virtual std::string status () const = 0;
    virtual std::string path () const = 0;
    virtual int channel () const = 0;
    virtual bool hasArg (int channel, const std::string &arg) const = 0;

    /* Reads the profile again; false, with why, if it is wrong. */
    virtual bool reload (std::string &err) = 0;
};

#endif /* THH_PANE_H */
