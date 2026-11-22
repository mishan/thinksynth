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
 * camera's own profile with the player's over it, as the desktop plays it
 * (hid/thhCamera.h). What comes back is the notes, in order, the zones'
 * levels for the page to light, the zones struck, for the camera view to
 * flash, and the controls' values, for the configurator's form.
 *
 * NOTES ONLY. A profile's values ride a patch's args in the args' own
 * ranges, which this realm has no patch to ask, and a bend or an aux the
 * page plays only for the theremin; the form offers neither.
 */

#include <stdint.h>
#include <stdio.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <emscripten.h>

#include "thHidBlock.h"
#include "thhCamera.h"
#include "thhMapper.h"
#include "twprofile.h"

namespace {

std::unique_ptr<thMotion> motion_;
thhCamera                 camera_;
thhMapper                 mapper_;
std::vector<uint8_t>      luma_;
std::string               said_;
uint32_t                  buttons_;
std::vector<std::pair<int, float>> hits_;
thhState                  last_;

/* {"notes":[[note,velocity],...],"zones":[...],"hits":[[zone,strike],...],
   "down":bits,"values":[...]}, and "problem" with the player's profile if
   given: a velocity of 0 lets the note go; a zone 0..8, struck as hard as
   strike, 0..1; bit z set while zone z has struck and not yet gone still,
   when it can't strike again; the values as twValues has them. */
const char *
say (const std::vector<thhAction> &acts, const float *zones,
     const std::string *problem = NULL)
{
    said_ = "{\"notes\":[";
    for (size_t i = 0; i < acts.size(); i++)
        if (acts[i].kind == thhAction::NOTE_ON || acts[i].kind == thhAction::NOTE_OFF)
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
    said_ += "],\"hits\":[";
    for (size_t i = 0; i < hits_.size(); i++)
    {
        char hit[32];

        snprintf(hit, sizeof(hit), "%s[%d,%.3f]", i ? "," : "",
                 hits_[i].first, hits_[i].second);
        said_ += hit;
    }
    said_ += "],\"down\":" +
             std::to_string((buttons_ >> THH_CAMERA_HIT1) & 0x1ff) +
             ",\"values\":" + twValues(thhCameraControls(), last_);
    said_ += (problem != NULL ? twProblem(*problem) : "") + "}";

    return said_.c_str();
}

void
play (const float *axes, double seconds, bool connected,
      std::vector<thhAction> &acts)
{
    std::vector<thhState> states;

    hits_.clear();
    camera_.update(axes, seconds, connected, states);
    for (const auto &s : states)
    {
        const uint32_t down = s.buttons & ~buttons_;

        for (int z = 0; z < 9; z++)
            if (down & (1u << (THH_CAMERA_HIT1 + z)))
                hits_.push_back({ z, s.axis[THH_CAMERA_STRIKE] });
        buttons_ = s.buttons;
        last_ = s;
        mapper_.update(s, 0, acts);
    }
}

} /* namespace */

extern "C" {

/* The camera's own profile with the player's `text' over it, onto a fresh
   mapper: what is wrong with the player's, or "". */
EMSCRIPTEN_KEEPALIVE const char *tc_start (const char *text)
{
    std::vector<thhAction> none;

    mapper_ = thhMapper();
    camera_ = thhCamera();
    buttons_ = 0;
    last_ = thhState();
    motion_.reset();
    said_ = twConfigure(mapper_, thhCameraControls(), THH_CAMERA_PROFILE, text, none);
    return said_.c_str();
}

/* The player's profile changed while the camera plays: what it lets go
   of, as tc_feed says it, with "problem". */
EMSCRIPTEN_KEEPALIVE const char *tc_profile (const char *text)
{
    std::vector<thhAction> acts;
    const std::string problem = twConfigure(mapper_, thhCameraControls(),
                                            THH_CAMERA_PROFILE, text, acts);

    hits_.clear();
    return say(acts, NULL, &problem);
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

    hits_.clear();
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
