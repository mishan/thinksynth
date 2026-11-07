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

#ifndef TH_MOTION_H
#define TH_MOTION_H 1

#include <stdint.h>

#include <vector>

#include "thExport.h"

/* How a camera's picture is moving, as a handful of numbers a player can
 * play: how much, where, how spread out, and in which of nine zones.
 *
 * Fed grayscale frames from any source -- a decoded webcam frame on the
 * desktop, a canvas in the browser -- so it knows nothing about cameras. A
 * frame is compared with an earlier one; a pixel that changed by more than
 * the sensor's own noise is motion, weighted by how much it changed.
 *
 * AS THE PLAYER SEES IT. The camera faces the player, so the left of the
 * picture is the player's right. x and the zones are mirrored: moving your
 * right hand to your right raises x, and zone 1 is the top left of the
 * player's own view, numbered like a phone's keypad.
 *
 * OVER A THIRTIETH OF A SECOND, WHATEVER THE FRAME RATE. Each frame is
 * compared with the newest kept frame at least COMPARE_SECONDS older, and
 * the difference is divided by the time between them. Comparing neighbors
 * would let a slow hand at 120 fps change each pixel by less than the noise
 * floor and vanish, and would read a frame a browser hands over twice as
 * stillness. The kept frames reach back HISTORY frames, so past 240 fps
 * nothing is compared.
 *
 * ONE SCALE FOR THE FRAME AND THE ZONES. Both are measured against a
 * hand-sized patch, HAND_SHARE of the frame, so a hand in one zone reads
 * the same there as in the whole-frame motion and neither clips the
 * velocity a strike is made from.
 *
 * LIGHT IS NOT MOTION. The change in the frame's mean brightness is taken
 * off every pixel's difference first, so auto-exposure stepping when a hand
 * comes in does not fire every zone at once.
 *
 * STILLNESS. With nothing moving, motion and the zones fall to 0, but x, y
 * and spread move only while motion is above PLACE_MOTION: a theremin whose
 * pitch followed a few noisy pixels to a corner whenever the hand paused
 * would be unplayable. They start in the middle. A frame more than
 * MAX_GAP_SECONDS after the one it would be compared with is compared with
 * nothing, since a difference across a stall is a jump cut and not a
 * gesture; one stamped no later than the last starts the history over.
 *
 * One thread; allocates only when made. */
class THINK_API thMotion {
public:
    enum Axis {
        MOTION, X, Y, SPREAD, BRIGHT,
        ZONE1, ZONE2, ZONE3, ZONE4, ZONE5, ZONE6, ZONE7, ZONE8, ZONE9,
        AXES
    };

    /* A difference of this many levels out of 255 is sensor noise. */
    static const int NOISE_LEVELS = 12;
    static const int HISTORY = 8;
    static constexpr double COMPARE_SECONDS = 0.03;
    static constexpr double MAX_GAP_SECONDS = 0.25;

    /* Motion is 1 when a patch of HAND_SHARE of the frame changes this
       much a second: about a hand at arm's length crossing ten of its own
       widths a second. */
    static constexpr double HAND_SHARE = 0.05;
    static constexpr double FULL_MOTION_PER_SECOND = 14;
    static constexpr double PLACE_MOTION = 0.02;

    /* Smaller than 3x3, a frame has no zones and nothing is measured. */
    thMotion (int width, int height);

    /* `stride' is the bytes from one row to the next; `seconds' the frame's
       capture time. */
    void feed (const uint8_t *luma, int stride, double seconds);

    /* 0..1 */
    float axis (Axis a) const { return axes_[a]; }

    /* The name a profile uses: "motion", "x", ..., "zone_1" .. "zone_9". */
    static const char *axisName (Axis a);

private:
    int width_, height_;
    std::vector<uint8_t> frames_;           /* HISTORY frames, a ring */
    double seconds_[HISTORY], means_[HISTORY];
    int newest_, kept_;
    float axes_[AXES];
};

#endif /* TH_MOTION_H */
