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
 * twcamera -- a webcam played as a controller, in the camera worker's own
 * instance of the module (camworker.js).
 *
 * The page draws each frame small and hands its luma here: thMotion reads
 * it, thhCamera makes a controller's state of it, and thhMapper plays the
 * camera's own profile, as the desktop plays it (hid/thhCamera.h). What
 * comes back is the notes, in order, and the zones' levels for the page to
 * light.
 *
 * NOTES ONLY. A profile's values ride a patch's args in the args' own
 * ranges, which this realm has no patch to ask; the camera's own profile
 * has none.
 */

#include <stdint.h>
#include <stdio.h>

#include <memory>
#include <string>
#include <vector>

#include <emscripten.h>

#include "thcHidBlock.h"
#include "thhCamera.h"
#include "thhMapper.h"

namespace {

std::unique_ptr<thMotion> motion_;
thhCamera                 camera_;
thhMapper                 mapper_;
std::vector<uint8_t>      luma_;
std::string               said_;

/* {"notes":[[note,velocity],...],"zones":[...]}: a velocity of 0 lets the
   note go. */
const char *
say (const std::vector<thhAction> &acts, const float *zones)
{
    said_ = "{\"notes\":[";
    for (size_t i = 0; i < acts.size(); i++)
        if (acts[i].kind != thhAction::VALUE)
            said_ += (said_.back() == '[' ? "[" : ",[") +
                     std::to_string(acts[i].note) + "," +
                     std::to_string(acts[i].kind == thhAction::NOTE_ON ? acts[i].vel : 0) +
                     "]";
    said_ += "],\"zones\":[";
    for (int z = 0; z < 9; z++)
    {
        char level[16];

        snprintf(level, sizeof(level), "%s%.3f", z ? "," : "", zones ? zones[z] : 0.0f);
        said_ += level;
    }
    said_ += "]}";

    return said_.c_str();
}

void
play (const float *axes, double seconds, bool connected,
      std::vector<thhAction> &acts)
{
    std::vector<thhState> states;

    camera_.update(axes, seconds, connected, states);
    for (const auto &s : states)
        mapper_.update(s, 0, acts);
}

} /* namespace */

extern "C" {

/* The camera's own profile, onto a fresh mapper. "" if it binds, else what
   is wrong, which would be this build's fault. */
EMSCRIPTEN_KEEPALIVE const char *tc_start (void)
{
    thcHidBlock profile;
    std::vector<thhAction> none;
    int line = 0;

    said_.clear();
    mapper_ = thhMapper();
    camera_ = thhCamera();
    motion_.reset();
    if (thcHidParseProfile(THH_CAMERA_PROFILE, profile, said_, line))
        mapper_.configure(profile, thhCameraControls(), none, said_, line);

    return said_.c_str();
}

/* Where to write a width x height frame's luma, a byte a pixel, before
   tc_feed. */
EMSCRIPTEN_KEEPALIVE uint8_t *tc_luma (int width, int height)
{
    luma_.resize((size_t)width * height);
    return luma_.data();
}

/* The frame in tc_luma, captured at `seconds'. */
EMSCRIPTEN_KEEPALIVE const char *tc_feed (int width, int height,
                                          double seconds)
{
    std::vector<thhAction> acts;

    if (width < 3 || height < 3 || luma_.size() != (size_t)width * height)
        return say(acts, NULL);

    if (!motion_ || motion_->width() != width || motion_->height() != height)
        motion_.reset(new thMotion(width, height));
    motion_->feed(luma_.data(), width, seconds);

    float axes[thMotion::AXES];

    for (int a = 0; a < thMotion::AXES; a++)
        axes[a] = motion_->axis((thMotion::Axis)a);
    play(axes, seconds, true, acts);

    return say(acts, axes + thMotion::ZONE1);
}

/* The camera gone, stopped or stalled: every note it holds lets go, and
   the next frame starts afresh. */
EMSCRIPTEN_KEEPALIVE const char *tc_gone (void)
{
    std::vector<thhAction> acts;

    motion_.reset();
    play(NULL, 0, false, acts);

    return say(acts, NULL);
}

} /* extern "C" */
