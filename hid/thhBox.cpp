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
#include <cstring>

#include "thhBox.h"

/* Gravity over this many seconds, which a hand's tremor doesn't move. */
#define THH_BOX_GRAVITY_S 0.07f
/* The twist's: quick enough to follow a flick. */
#define THH_BOX_TWIST_S 0.006f
/* Degrees a second at full twist, and the turn's quarter turn. */
#define THH_BOX_TWIST_FULL 500.0f
#define THH_BOX_TURN_FULL 90.0f
#define THH_BOX_TURN_SPRING 3.0f

const char *const THH_BOX_PROFILE =
    "device box {\n"
    "    base = \"C4\";\n"
    "    touch = note 0;\n"
    "    x = pitch { scale = minor_pentatonic; range = 10; };\n"
    "    y = velocity { min = 30; max = 127; };\n"
    "    tilt = bend { range = 2; smooth = 0.03; };\n"
    "};\n";

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

/* Upright and facing you, up is the sensor's +x; the screen faces -z,
   and its right edge +y. */
static const float UP[3] = { 1, 0, 0 };

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
        /* Gravity turned as the pose leveled is to upright, so both count
           from it. Tilt is the angle about the screen's left-right axis,
           all the way round, so it holds at a quarter turn rather than
           folding back past it; scaled down as the BOX rolls, as asin's
           was, and to none on its side, where that angle is noise. */
        float u[3];

        for (int i = 0; i < 3; i++)
            u[i] = (imu.level[i][0] * imu.g[0] + imu.level[i][1] * imu.g[1] +
                    imu.level[i][2] * imu.g[2]) / g;
        s.axis[THH_BOX_TILT] = clamp1(std::atan2(-u[2], u[0]) / (float)(M_PI / 2) *
                                      std::hypot(u[0], u[2]));
        s.axis[THH_BOX_ROLL] = clamp1(std::asin(clamp1(-u[1])) / (float)(M_PI / 2));
    }
    s.axis[THH_BOX_TWIST] = clamp1(imu.twist);
    s.axis[THH_BOX_TURN] = clamp1(imu.turn / THH_BOX_TURN_FULL);
}

void
thhBoxLevel (thhBoxImu &imu)
{
    const float g = std::sqrt(imu.g[0] * imu.g[0] + imu.g[1] * imu.g[1] +
                              imu.g[2] * imu.g[2]);

    if (g <= 0)
        return;

    /* The least turn taking gravity now to upright's (Rodrigues'), or
       half a turn about the left-right axis from upside down. */
    const float a[3] = { imu.g[0] / g, imu.g[1] / g, imu.g[2] / g };
    const float v[3] = { a[1] * UP[2] - a[2] * UP[1], a[2] * UP[0] - a[0] * UP[2],
                         a[0] * UP[1] - a[1] * UP[0] };
    const float c = a[0] * UP[0] + a[1] * UP[1] + a[2] * UP[2];

    if (c < -0.9999f)
    {
        const float half[3][3] = { { -1, 0, 0 }, { 0, 1, 0 }, { 0, 0, -1 } };

        std::memcpy(imu.level, half, sizeof(half));
        return;
    }

    const float k = 1 / (1 + c), vv = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    const float cross[3][3] = { { 0, -v[2], v[1] }, { v[2], 0, -v[0] }, { -v[1], v[0], 0 } };

    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            imu.level[i][j] = (i == j) * (1 - k * vv) + cross[i][j] + k * v[i] * v[j];
}
