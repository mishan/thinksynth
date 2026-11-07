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
 * frame is compared with the one before it; a pixel that changed by more
 * than the sensor's own noise is motion, weighted by how much it changed.
 *
 * AS THE PLAYER SEES IT. The camera faces the player, so the left of the
 * picture is the player's right. x and the zones are mirrored: moving your
 * right hand to your right raises x, and zone 1 is the top left of the
 * player's own view, numbered like a phone's keypad.
 *
 * PER SECOND, NOT PER FRAME. A hand sweeps fewer pixels between two frames
 * at 120 fps than at 30, so a difference per frame would make the same
 * gesture four times quieter on a faster camera. Motion is the difference
 * divided by the time between the frames' timestamps.
 *
 * STILLNESS. With nothing moving, motion and the zones fall to 0 at the next
 * frame, but x, y and spread keep the last place something moved: a theremin
 * whose pitch jumped to a corner whenever the hand paused would be
 * unplayable. A frame more than MAX_GAP_SECONDS after the last one, or not
 * after it at all, is compared with nothing, since a difference across a
 * stall is a jump cut and not a gesture.
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
    static constexpr double MAX_GAP_SECONDS = 0.25;

    /* Motion is 1 at this much difference per second: the mean, over the
       frame or the zone, of each moving pixel's change out of 255. */
    static constexpr double FULL_MOTION_PER_SECOND = 1.5;

    /* At least 3x3, so every zone has a pixel. */
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
    std::vector<uint8_t> prev_;
    bool havePrev_;
    double prevSeconds_;
    float axes_[AXES];
};

#endif /* TH_MOTION_H */
