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

#include <algorithm>
#include <cmath>

#include "thhPhone.h"

const char *const THH_PHONE_PROFILE =
    "device phone {\n"
    "    base = \"C3\";\n"
    "    touch_1 = note 0;\n"
    "    touch_1_x = bend { scale = pentatonic; range = 15; smooth = 0.05; };\n"
    "    touch_1_y = aux 3 { min = 1; max = 0; smooth = 0.03; };\n"
    "    touch_2 = octave 1;\n"
    "    touch_3 = octave -1;\n"
    "    tilt_x = aux 1 { smooth = 0.05; };\n"
    "};\n";

const thhControls &
thhPhoneControls ()
{
    static const thhControls c = {
        "phone",
        { "touch_1", "touch_2", "touch_3", "touch_4", "touch_5" },
        {
            { "tilt_x", true }, { "tilt_y", true },
            { "touch_1_x", false }, { "touch_1_y", false },
            { "touch_2_x", false }, { "touch_2_y", false },
            { "touch_3_x", false }, { "touch_3_y", false },
            { "touch_4_x", false }, { "touch_4_y", false },
            { "touch_5_x", false }, { "touch_5_y", false },
        },
    };

    return c;
}

namespace {

/* Every axis a number, and no finger the phone hasn't. A frame is the
   phone's word for everything at once, so one wrong value is reason to
   doubt the rest. A value past its range is not: gravity read off a phone
   that is shaken is more than gravity, and a finger held as it slides off
   the screen is off it. */
bool
wellFormed (const thhPhoneFrame &f)
{
    if (f.down >> THH_PHONE_BUTTONS)
        return false;
    for (int a = 0; a < THH_PHONE_AXES; a++)
        if (!std::isfinite(f.axes[a]))
            return false;
    return true;
}

}

bool
thhPhone::take (const thhPhoneFrame &f, double seconds)
{
    /* Later by the sequence's own arithmetic, so its wrap is no reset. */
    const bool later = !have_ || f.id != last_.id ||
                       (int32_t)(f.seq - last_.seq) > 0;

    if (!wellFormed(f) || !later)
    {
        dropped_++;
        return false;
    }
    have_ = true;
    last_ = f;
    for (int a = 0; a < THH_PHONE_AXES; a++)
        last_.axes[a] = std::clamp(f.axes[a],
                                   a < THH_PHONE_TOUCH_1_X ? -1.0f : 0.0f, 1.0f);
    at_ = seconds;
    return true;
}

thhState
thhPhone::state (double seconds) const
{
    thhState s;

    s.time = seconds;
    s.connected = have_ && seconds - at_ < GONE_SECONDS;
    if (!s.connected)
        return s;
    for (int a = 0; a < THH_PHONE_AXES; a++)
        s.axis[a] = last_.axes[a];
    s.buttons = last_.down;
    return s;
}
