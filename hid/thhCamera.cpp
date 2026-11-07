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
#include <string>

#include "thhCamera.h"

/* An air harp: the bottom row is the lowest, as a player's hands rest low,
   and each row climbs a pentatonic scale from left to right. */
const char *const THH_CAMERA_PROFILE =
    "device camera {\n"
    "    base = \"C4\";\n"
    "    hit_7 = note 0;  hit_8 = note 2;  hit_9 = note 4;\n"
    "    hit_4 = note 7;  hit_5 = note 9;  hit_6 = note 12;\n"
    "    hit_1 = note 14; hit_2 = note 16; hit_3 = note 19;\n"
    "    strike = velocity { min = 40; max = 127; };\n"
    "};\n";

const thhControls &
thhCameraControls ()
{
    static const thhControls c = [] {
        thhControls k;

        k.device = "camera";
        for (int z = 1; z <= 9; z++)
            k.buttons.push_back("hit_" + std::to_string(z));
        k.buttons.push_back("moving");
        for (int a = 0; a < thMotion::AXES; a++)
            k.axes.push_back({ thMotion::axisName((thMotion::Axis)a), false });
        k.axes.push_back({ "strike", false });
        return k;
    }();

    return c;
}

thhCamera::thhCamera ()
{
    std::fill(rising_, rising_ + 9, false);
    std::fill(peak_, peak_ + 9, 0.0f);
    std::fill(since_, since_ + 9, 0.0);
}

void
thhCamera::update (const float *axes, double seconds, bool connected,
                   std::vector<thhState> &out)
{
    if (!connected)
    {
        *this = thhCamera();
        out.push_back(state_);
        return;
    }

    thhState &s = state_;
    const uint32_t moving = 1u << THH_CAMERA_MOVING;
    float strikes[9];
    int struck = 0;

    s.connected = true;
    for (int a = 0; a < thMotion::AXES; a++)
        s.axis[a] = axes[a];

    if (axes[thMotion::MOTION] > MOVING_ON)
        s.buttons |= moving;
    else if (axes[thMotion::MOTION] < MOVING_OFF)
        s.buttons &= ~moving;

    for (int z = 0; z < 9; z++)
    {
        const float level = axes[thMotion::ZONE1 + z];
        const uint32_t bit = 1u << (THH_CAMERA_HIT1 + z);

        strikes[z] = -1;
        if (s.buttons & bit)
        {
            if (level < HIT_OFF)
                s.buttons &= ~bit;
        }
        else
        {
            if (!rising_[z])
            {
                if (level < HIT_ON)
                    continue;
                rising_[z] = true;
                peak_[z] = level;
                since_[z] = seconds;
                if (level < STRIKE_FULL)
                    continue;
            }

            /* Short of the wait by a rounding error is the wait: 120 fps
               stamps are never exact. A stamp before the crossing ends it,
               or a rising level would hold the press for as long. */
            const double waited = seconds - since_[z];

            if (level > peak_[z] && waited >= 0 && waited < PEAK_WAIT_SECONDS - 1e-6)
            {
                peak_[z] = level;
                continue;
            }

            const float peak = std::max(peak_[z], level);

            rising_[z] = false;
            strikes[z] = std::min(1.0f, (peak - HIT_ON) / (STRIKE_FULL - HIT_ON));
            struck++;
        }
    }

    for (int z = 0; z < 9; z++)
        if (strikes[z] >= 0)
        {
            s.buttons |= 1u << (THH_CAMERA_HIT1 + z);
            s.axis[THH_CAMERA_STRIKE] = strikes[z];
            out.push_back(s);
        }
    if (!struck)
        out.push_back(s);
}
