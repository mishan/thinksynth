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
 */

/*
 * twhands -- two hands a camera's tracker found, played as a theremin, in
 * the camera worker's own instance of the module (camworker.js).
 *
 * The page hands over each frame's hands as the tracker gave them (into
 * hd_hands); thhHandTrack makes a controller's state of them and thhMapper
 * plays the hands' own profile (hid/thhHandTrack.h). What comes back is the
 * notes, their bends and their aux values, in order, for the page to play
 * as the Valve Index's are played, and where each hand is, for the camera
 * view to draw.
 */

#include <cstdio>
#include <string>
#include <vector>

#include <emscripten.h>

#include "thcHidBlock.h"
#include "thhHandTrack.h"
#include "thhMapper.h"

namespace {

/* A hand in hd_hands: its 21 x, its 21 y, the tracker's word for which it
   is (0 "Left", 1 "Right", else none) and how sure it is. */
const int HAND = 44;

thhHandTrack       track_;
thhMapper          mapper_;
std::vector<float> hands_;
std::string        said_;

void
add (const char *fmt, double a, double b = 0, double c = 0)
{
    char one[64];

    snprintf(one, sizeof(one), fmt, a, b, c);
    said_ += one;
}

/* {"notes":[[note,velocity],...],"bends":[[note,semitones],...],
   "aux":[[note,which,value],...],"left":[x,y,close,pinched]|null,
   "right":...}: a velocity of 0 lets the note go. */
const char *
say (const std::vector<thhAction> &acts, const thhState &s)
{
    static const thhAction::Kind KINDS[3] = {
        thhAction::NOTE_ON, thhAction::NOTE_BEND, thhAction::NOTE_AUX };
    static const char *const NAMES[3] = { "notes", "bends", "aux" };

    said_ = "{";
    for (int k = 0; k < 3; k++)
    {
        bool first = true;

        said_ += std::string(k ? "]," : "") + "\"" + NAMES[k] + "\":[";
        for (const thhAction &a : acts)
        {
            const bool note = a.kind == thhAction::NOTE_ON ||
                              a.kind == thhAction::NOTE_OFF;

            if (k == 0 ? !note : a.kind != KINDS[k])
                continue;
            said_ += first ? "" : ",";
            first = false;
            if (k == 0)
                add("[%.0f,%.0f]", a.note, a.kind == thhAction::NOTE_ON ? a.vel : 0);
            else if (k == 1)
                add("[%.0f,%.3f]", a.note, a.t);
            else
                add("[%.0f,%.0f,%.3f]", a.note, a.aux, a.t);
        }
    }
    said_ += "]";

    static const struct { const char *name; int x, pinch, seen; } SIDES[2] = {
        { "left", THH_HANDS_LEFT_X, THH_HANDS_LEFT_PINCH, THH_HANDS_LEFT_SEEN },
        { "right", THH_HANDS_RIGHT_X, THH_HANDS_RIGHT_PINCH, THH_HANDS_RIGHT_SEEN },
    };

    for (const auto &side : SIDES)
    {
        said_ += std::string(",\"") + side.name + "\":";
        if (!(s.buttons & (1u << side.seen)))
        {
            said_ += "null";
            continue;
        }
        add("[%.3f,%.3f,", s.axis[side.x], s.axis[side.x + 1]);
        add("%.3f,%.0f]", s.axis[side.x + 2], s.buttons & (1u << side.pinch) ? 1 : 0);
    }
    said_ += "}";
    return said_.c_str();
}

} /* namespace */

extern "C" {

/* The hands' own profile, snapped to its scale or not, onto a fresh
   mapper. "" if it binds, else what is wrong, which would be this build's
   fault. */
EMSCRIPTEN_KEEPALIVE const char *hd_start (int snap)
{
    thcHidBlock profile;
    std::vector<thhAction> none;
    int line = 0;

    said_.clear();
    track_ = thhHandTrack();
    mapper_ = thhMapper();
    if (thcHidParseProfile(snap ? THH_HANDS_PROFILE : THH_HANDS_FREE_PROFILE,
                           profile, said_, line))
        mapper_.configure(profile, thhHandsControls(), none, said_, line);

    return said_.c_str();
}

/* Where to write n hands, HAND floats each, before hd_feed. */
EMSCRIPTEN_KEEPALIVE float *hd_hands (int n)
{
    hands_.resize((size_t)(n > 0 ? n : 0) * HAND);
    return hands_.data();
}

/* The n hands in hd_hands, seen at `seconds'. */
EMSCRIPTEN_KEEPALIVE const char *hd_feed (int n, double seconds)
{
    std::vector<thhHandPoints> hands;
    std::vector<thhAction> acts;
    thhState s;

    for (int h = 0; h < n && (size_t)(h + 1) * HAND <= hands_.size(); h++)
    {
        const float *f = &hands_[(size_t)h * HAND];
        thhHandPoints p = thhHandPoints();

        for (int i = 0; i < 21; i++)
        {
            p.x[i] = f[i];
            p.y[i] = f[21 + i];
        }
        p.side = f[42] == 0 ? "Left" : f[42] == 1 ? "Right" : "";
        p.sure = f[43];
        hands.push_back(p);
    }
    track_.update(hands, seconds, true, s);
    mapper_.update(s, 0, acts);
    return say(acts, s);
}

/* The camera gone: every note let go. */
EMSCRIPTEN_KEEPALIVE const char *hd_gone (void)
{
    std::vector<thhAction> acts;
    thhState s;

    track_.update({}, 0, false, s);
    mapper_.update(s, 0, acts);
    return say(acts, s);
}

} /* extern "C" */
