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

#include <math.h>
#include <stdlib.h>

#include "thMotion.h"

thMotion::thMotion (int width, int height)
    : width_(width), height_(height), prev_((size_t)width * height),
      havePrev_(false), prevSeconds_(0)
{
    for (int i = 0; i < AXES; i++)
        axes_[i] = 0;
}

void thMotion::feed (const uint8_t *luma, int stride, double seconds)
{
    const double dt = seconds - prevSeconds_;
    const bool compare = havePrev_ && dt > 0 && dt <= MAX_GAP_SECONDS;

    double zone[9] = { 0 };
    long zonePixels[9] = { 0 };
    double sum = 0, total = 0, sx = 0, sy = 0, sxx = 0, syy = 0;

    for (int y = 0; y < height_; y++)
    {
        const uint8_t *row = luma + (size_t)y * stride;
        uint8_t *was = &prev_[(size_t)y * width_];
        const int zy = 3 * y / height_;

        for (int x = 0; x < width_; x++)
        {
            /* Mirrored: the player's left is the picture's right. */
            const int z = zy * 3 + 2 - 3 * x / width_;

            total += row[x];
            zonePixels[z]++;

            const int d = abs((int)row[x] - (int)was[x]);

            was[x] = row[x];

            if (!compare || d <= NOISE_LEVELS)
                continue;

            const double px = 1 - (x + 0.5) / width_, py = (y + 0.5) / height_;

            sum += d;
            sx += d * px;
            sy += d * py;
            sxx += d * px * px;
            syy += d * py * py;
            zone[z] += d;
        }
    }

    axes_[BRIGHT] = (float)(total / ((double)width_ * height_ * 255));

    const double full = FULL_MOTION_PER_SECOND * 255 * dt;

    axes_[MOTION] = compare ? (float)fmin(1, sum / ((double)width_ * height_ * full)) : 0;

    for (int z = 0; z < 9; z++)
        axes_[ZONE1 + z] = compare ? (float)fmin(1, zone[z] / (zonePixels[z] * full)) : 0;

    if (sum > 0)
    {
        const double mx = sx / sum, my = sy / sum;

        axes_[X] = (float)mx;
        axes_[Y] = (float)my;
        /* RMS distance from the centroid, out of the most a frame allows:
           half its diagonal. */
        axes_[SPREAD] = (float)fmin(1, sqrt(fmax(0, sxx / sum - mx * mx + syy / sum - my * my))
                                       / sqrt(0.5));
    }

    havePrev_ = true;
    prevSeconds_ = seconds;
}

const char *thMotion::axisName (Axis a)
{
    static const char *const names[AXES] = {
        "motion", "x", "y", "spread", "bright",
        "zone_1", "zone_2", "zone_3", "zone_4", "zone_5",
        "zone_6", "zone_7", "zone_8", "zone_9"
    };

    return names[a];
}
