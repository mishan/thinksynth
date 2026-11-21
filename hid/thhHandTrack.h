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

#ifndef THH_HAND_TRACK_H
#define THH_HAND_TRACK_H

#include <string>
#include <vector>

#include "thhControls.h"

/* Two hands a camera's tracker found, as a controller: the device `hands'.

   A hand is the tracker's 21 points (MediaPipe's: 0 the wrist, 4 the
   thumb's tip, 5, 9, 13 and 17 the knuckles, 8 the index finger's tip) as
   fractions of the camera's own picture, which is the player's mirrored.
   Positions here are the player's: x across from their left, y up from the
   bottom of the picture, both of the palm (the wrist and the knuckles), so
   closing the fingers doesn't move them. close is how near the thumb's and
   index finger's tips are, over the hand's size: 0 apart, 1 touching.

   Which hand is which is kept rather than read afresh each frame, since a
   tracker's own word for it flickers. A hand seen is the nearer of those
   held, if one is within a quarter of the picture: two hands held stay the
   two they were, so one above the other keeps its side as they pass
   across. Two hands new are left and right as they lie; one new hand is
   what the tracker calls it if the tracker is sure, else the side of the
   picture it is on. A hand that
   vanishes keeps its values LOST_SECONDS, which a tracker's dropped frame
   isn't longer than, and is then gone, its pinch up. */

enum thhHandsButton
{
    THH_HANDS_LEFT_PINCH,
    THH_HANDS_RIGHT_PINCH,
    THH_HANDS_LEFT_SEEN,
    THH_HANDS_RIGHT_SEEN,
    THH_HANDS_BUTTONS
};

enum thhHandsAxis
{
    THH_HANDS_LEFT_X,
    THH_HANDS_LEFT_Y,
    THH_HANDS_LEFT_CLOSE,
    THH_HANDS_RIGHT_X,
    THH_HANDS_RIGHT_Y,
    THH_HANDS_RIGHT_CLOSE,
    THH_HANDS_AXES
};

const thhControls &thhHandsControls ();

/* A theremin: the left hand's pinch plays, the right hand's height bends the
   note along a scale, and how far the left hand is below the top is its
   aux3, which dsp/theremin.dsp hushes it by. A hush, so that a key's note,
   whose aux3 is 0, plays out; and aux3, which no other patch reads, so the
   left hand changes nothing it wasn't meant to (docs/DSP_FORMAT.md). */
extern const char *const THH_HANDS_PROFILE;
extern const char *const THH_HANDS_FREE_PROFILE;

/* One hand as the tracker gave it. `side' is its word for which hand,
   "Left" or "Right", for a mirrored picture, and `sure' how sure, 0 to 1. */
struct thhHandPoints
{
    float       x[21], y[21];
    std::string side;
    float       sure;
};

class thhHandTrack
{
public:
    static constexpr float PINCH_ON = 0.75f, PINCH_OFF = 0.5f;
    static constexpr double LOST_SECONDS = 0.15;

    /* A frame's hands, at `seconds', into `out'; a camera gone (connected
       false) forgets both hands. */
    void update (const std::vector<thhHandPoints> &hands, double seconds,
                 bool connected, thhState &out);

private:
    struct Track
    {
        bool   held = false;
        double seen = 0;
        float  x = 0, y = 0, close = 0;
        bool   pinched = false;
    };

    Track tracks_[2];           /* left, right */
};

#endif /* THH_HAND_TRACK_H */
