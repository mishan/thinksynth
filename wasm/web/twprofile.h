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

/*
 * twprofile.h -- what the camera worker's controllers (twcamera.cpp,
 * twhands.cpp) share: the player's profile over a device's own, onto a
 * mapper, and a state's values as the configurator's form reads them
 * (profileform.js).
 */

#ifndef TW_PROFILE_H
#define TW_PROFILE_H

#include <stdio.h>

#include <string>
#include <vector>

#include "thhMapper.h"
#include "thhProfile.h"

/* The player's `text' over `own', onto `mapper', its releases into `acts':
   what is wrong with the player's, or nothing; what fits plays either
   way. */
static inline std::string
twConfigure (thhMapper &mapper, const thhControls &controls, const char *own,
             const std::string &text, std::vector<thhAction> &acts)
{
    thHidBlock profile;
    std::string err, configureErr;
    int line = 0;

    thhLoadProfileText(text, "your profile", controls, own, "", profile, err);
    if (!mapper.configure(profile, controls, acts, configureErr, line))
        err += (err.empty() ? "" : "; ") + configureErr;
    return err;
}

/* `s''s buttons, 0 or 1, then its axes, as `controls' has them; none while
   it isn't connected. */
static inline std::string
twValues (const thhControls &controls, const thhState &s)
{
    std::string out = "[";

    for (size_t i = 0; s.connected && i < controls.buttons.size(); i++)
        out += std::string(i ? "," : "") + ((s.buttons >> i) & 1 ? "1" : "0");
    for (size_t i = 0; s.connected && i < controls.axes.size(); i++)
    {
        char v[24];

        snprintf(v, sizeof(v), ",%.3f", s.axis[i]);
        out += v;
    }
    return out + "]";
}

/* `,"problem":"..."', what is wrong with the player's profile, for the
   end of a JSON object. */
static inline std::string
twProblem (const std::string &problem)
{
    std::string out = ",\"problem\":\"";

    for (char c : problem)
        out += c == '"' || c == '\\' ? std::string("\\") + c
             : (unsigned char)c < 0x20 ? std::string(" ") : std::string(1, c);
    return out + "\"";
}

#endif /* TW_PROFILE_H */
