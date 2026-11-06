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

#include "thhControls.h"

int
thhControls::button (const std::string &name) const
{
    for (size_t i = 0; i < buttons.size(); i++)
        if (buttons[i] == name)
            return (int)i;
    return -1;
}

int
thhControls::axis (const std::string &name) const
{
    for (size_t i = 0; i < axes.size(); i++)
        if (axes[i].name == name)
            return (int)i;
    return -1;
}

const thhControls &
thhIndexControls ()
{
    static const thhControls c = {
        "index",
        { "a", "b", "system", "trigger_click", "thumbstick", "trackpad" },
        {
            { "index", false }, { "middle", false }, { "ring", false },
            { "pinky", false }, { "grip", false }, { "trigger", false },
            { "pad_x", true }, { "pad_y", true }, { "pad_force", false },
            { "stick_x", true }, { "stick_y", true },
        },
    };

    return c;
}
