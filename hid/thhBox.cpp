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

#include <cmath>

#include "thhBox.h"

/* Gravity over this many seconds, which a hand's tremor doesn't move. */
#define THH_BOX_GRAVITY_S 0.07f
/* The twist's: quick enough to follow a flick. */
#define THH_BOX_TWIST_S 0.006f
/* Degrees a second at full twist, and the turn's quarter turn. */
#define THH_BOX_TWIST_FULL 500.0f
#define THH_BOX_TURN_FULL 90.0f
#define THH_BOX_TURN_SPRING 3.0f

const thhControls &
thhBoxControls ()
{
    static const thhControls c = {
        "box",
        { "touch" },
        {
            { "x", false }, { "y", false },
            { "tilt", true }, { "roll", true }, { "twist", true },
            { "turn", true },
        },
    };

    return c;
}

static float
clamp1 (float v)
{
    return v < -1 ? -1 : v > 1 ? 1 : v;
}

void
thhBoxFeedImu (const float accel[3], const float gyro[3], float dt,
               thhBoxImu &imu, thhState &s)
{
    const bool afresh = !imu.primed || dt <= 0 || dt > 0.1f;
    const float kg = afresh ? 1 : 1 - std::exp(-dt / THH_BOX_GRAVITY_S);
    const float kt = afresh ? 1 : 1 - std::exp(-dt / THH_BOX_TWIST_S);

    for (int k = 0; k < 3; k++)
        imu.g[k] += kg * (accel[k] - imu.g[k]);
    imu.twist += kt * (gyro[2] / THH_BOX_TWIST_FULL - imu.twist);
    if (!afresh)
        imu.turn = (imu.turn + gyro[2] * dt) * std::exp(-dt / THH_BOX_TURN_SPRING);
    imu.primed = true;

    const float g = std::sqrt(imu.g[0] * imu.g[0] + imu.g[1] * imu.g[1] +
                              imu.g[2] * imu.g[2]);

    if (g > 0)
    {
        /* Upright and facing you, up is the sensor's -x; the screen faces
           -z, and its left edge +y. */
        s.axis[THH_BOX_TILT] = clamp1(std::asin(clamp1(-imu.g[2] / g)) / (float)(M_PI / 2));
        s.axis[THH_BOX_ROLL] = clamp1(std::asin(clamp1(imu.g[1] / g)) / (float)(M_PI / 2));
    }
    s.axis[THH_BOX_TWIST] = clamp1(imu.twist);
    s.axis[THH_BOX_TURN] = clamp1(imu.turn / THH_BOX_TURN_FULL);
}
