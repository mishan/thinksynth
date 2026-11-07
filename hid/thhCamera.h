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

#ifndef THH_CAMERA_H
#define THH_CAMERA_H

#include <vector>

#include "thMotion.h"
#include "thhControls.h"

/* A webcam as a controller: thMotion's axes, and buttons a profile can play
   notes with. A zone is struck, and its hit_N button goes down, when its
   level crosses HIT_ON; it comes up below HIT_OFF. `moving' is down while
   anything moves.

   A strike's level is still rising when it crosses HIT_ON, so the press
   waits for the peak, up to PEAK_WAIT_SECONDS, and `strike' carries it:
   0 at HIT_ON, 1 at STRIKE_FULL or over. Bound as the velocity source, it
   makes each hit as loud as its own zone was struck. */

enum thhCameraButton
{
    THH_CAMERA_HIT1,    /* to THH_CAMERA_HIT1 + 8, in thMotion's zone order */
    THH_CAMERA_MOVING = THH_CAMERA_HIT1 + 9,
    THH_CAMERA_BUTTONS
};

/* thMotion's axes in its order, then strike. */
enum { THH_CAMERA_STRIKE = thMotion::AXES, THH_CAMERA_AXES };

const thhControls &thhCameraControls ();

/* What the camera plays with no profile of the player's own. */
extern const char *const THH_CAMERA_PROFILE;

class thhCamera
{
public:
    static constexpr float HIT_ON = 0.25f, HIT_OFF = 0.1f;
    static constexpr float MOVING_ON = 0.05f, MOVING_OFF = 0.02f;
    static constexpr float STRIKE_FULL = 0.5f;
    static constexpr double PEAK_WAIT_SECONDS = 0.025;

    thhCamera ();

    /* One frame's axes (thMotion::AXES of them), or the camera gone, into
       out: a state for each zone struck in this frame, in zone order, so
       each press reads its own strike; or one state if none was. Feed them
       to the mapper in order. Gone, every button comes up. */
    void update (const float *axes, double seconds, bool connected,
                 std::vector<thhState> &out);

private:
    thhState state_;
    bool     rising_[9];
    float    peak_[9];
    double   since_[9];
};

#endif /* THH_CAMERA_H */
