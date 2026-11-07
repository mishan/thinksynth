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

#ifndef THH_BOX_H
#define THH_BOX_H

#include "thhControls.h"

/* An Espressif ESP32-S3-BOX as a controller, on the BOX itself: its
   touchscreen and its IMU (an ICM-42607). Portable, so the firmware and
   the desktop's tools both build it. */

enum
{
    THH_BOX_TOUCH = 0                   /* a button: the screen touched */
};

enum
{
    THH_BOX_X, THH_BOX_Y,               /* where, 0..1 across and down */
    THH_BOX_TILT, THH_BOX_ROLL,         /* -1..1, a quarter turn each way */
    THH_BOX_TWIST,                      /* how fast it's turning, -1..1 */
    THH_BOX_TURN                        /* how far it has turned, -1..1 */
};

const thhControls &thhBoxControls ();

/* What the BOX plays with no block of a patch's: the screen a strip of
   keys, C minor pentatonic two octaves across, louder the lower it's
   touched, and tilt a bend. A patch's `controller box' block goes over
   it, and says what its own knobs answer to. */
extern const char *const THH_BOX_PROFILE;

/* What the IMU's samples make of the BOX's pose, between samples. */
struct thhBoxImu
{
    float g[3] = { 0, 0, 0 };           /* gravity, smoothed, in g */
    float twist = 0;
    float turn = 0;                     /* degrees, springing back */
    bool  primed = false;
};

/* One sample: acceleration in g and rotation in degrees a second, each in
   the sensor's own axes, dt seconds after the last. Writes the pose axes
   of s. Standing upright with its screen facing you, it's at rest: tilt is
   the top edge tipping away (+) or toward you, roll the BOX turned like a
   wheel, clockwise (+) or anticlockwise, and twist and turn that same
   turning, as speed and as distance, springing back to 0 over a few
   seconds. */
void thhBoxFeedImu (const float accel[3], const float gyro[3], float dt,
                    thhBoxImu &imu, thhState &s);

#endif /* THH_BOX_H */
