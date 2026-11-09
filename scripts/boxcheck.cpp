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
 * boxcheck -- the ESP32-S3-BOX's pose, as hid/thhBox.h reads it from its
 * IMU: level wherever the BOX is when it's asked to be.
 */

#include <cmath>
#include <cstdio>

#include "thhBox.h"

static int failed = 0;

/* tilt and roll read with the BOX tipped by degrees, its top edge away,
   from upright and facing you, and then turned like a wheel, clockwise,
   by roll's degrees. */
static void
pose (thhBoxImu &imu, float degrees, float rollDegrees, float &tilt, float &roll)
{
    const float a = degrees * (float)M_PI / 180, b = rollDegrees * (float)M_PI / 180;
    const float up[3] = { -std::cos(a) * std::cos(b), std::sin(b),
                          -std::sin(a) * std::cos(b) };
    const float still[3] = { 0, 0, 0 };
    thhState s;

    for (int i = 0; i < 500; i++)
        thhBoxFeedImu(up, still, 0.004f, imu, s);
    tilt = s.axis[THH_BOX_TILT];
    roll = s.axis[THH_BOX_ROLL];
}

static void
report (const char *name, float got, float want)
{
    const bool ok = std::fabs(got - want) < 0.01f;

    printf("%s  %s (%.3f, want %.3f)\n", ok ? "ok  " : "FAIL", name, got, want);
    failed += !ok;
}

int
main ()
{
    /* Each from a fresh start, leveled first where it says, in degrees;
       a quarter turn reads 1. */
    static const struct
    {
        const char *name;
        bool        level;
        float       levelTilt, levelRoll, tilt, roll, wantTilt, wantRoll;
    } cases[] = {
        { "leaning back 20 degrees tilts it 20", false, 0, 0, 20, 0, 20, 0 },
        { "tipped past a quarter turn it holds there", false, 0, 0, 120, 0, 90, 0 },
        { "and the other way", false, 0, 0, -120, 0, -90, 0 },
        { "leveled leaning back 20, the same lean reads level", true, 20, 0, 20, 0, 0, 0 },
        { "and 20 degrees further reads as 20", true, 20, 0, 40, 0, 20, 0 },
        { "and upright reads as 20 the other way", true, 20, 0, 0, 0, -20, 0 },
        { "and rolled there reads as only rolled", true, 20, 0, 20, 30, 0, 30 },
        { "leveled flat on a desk, the top lifted 30 tilts it -30", true, 90, 0, 60, 0, -30, 0 },
        { "and the top tipped down 30 tilts it 30", true, 90, 0, 120, 0, 30, 0 },
        { "and tipped down past a quarter turn holds there", true, 90, 0, 200, 0, 90, 0 },
        { "and lifted past a quarter turn holds there", true, 90, 0, -20, 0, -90, 0 },
        { "and rolled 30 on the desk rolls it 30", true, 90, 0, 90, 30, 0, 30 },
    };

    for (const auto &c : cases)
    {
        thhBoxImu imu;
        float tilt, roll;
        char name[160];

        if (c.level)
        {
            pose(imu, c.levelTilt, c.levelRoll, tilt, roll);
            thhBoxLevel(imu);
        }
        pose(imu, c.tilt, c.roll, tilt, roll);
        snprintf(name, sizeof(name), "%s: tilt", c.name);
        report(name, tilt, c.wantTilt / 90);
        snprintf(name, sizeof(name), "%s: roll", c.name);
        report(name, roll, c.wantRoll / 90);
    }

    printf("%d failure%s\n", failed, failed == 1 ? "" : "s");
    return failed ? 1 : 0;
}
