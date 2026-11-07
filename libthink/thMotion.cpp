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
#include <string.h>

#include "thMotion.h"

thMotion::thMotion (int width, int height)
    : width_(width), height_(height),
      frames_((size_t)HISTORY * (width > 0 ? width : 0) * (height > 0 ? height : 0)),
      newest_(0), kept_(0)
{
    for (int i = 0; i < AXES; i++)
        axes_[i] = 0;

    axes_[X] = axes_[Y] = 0.5f;
}

void thMotion::feed (const uint8_t *luma, int stride, double seconds)
{
    if (width_ < 3 || height_ < 3)
        return;

    const size_t area = (size_t)width_ * height_;
    double total = 0;

    for (int y = 0; y < height_; y++)
        for (int x = 0; x < width_; x++)
            total += luma[(size_t)y * stride + x];

    const double mean = total / area;

    axes_[BRIGHT] = (float)(mean / 255);

    if (kept_ > 0 && seconds <= seconds_[newest_])
        kept_ = 0;

    int ref = -1;

    for (int k = 0; k < kept_ && ref < 0; k++)
    {
        const int i = (newest_ - k + HISTORY) % HISTORY;

        if (seconds - seconds_[i] >= COMPARE_SECONDS)
            ref = i;
    }

    const double dt = ref >= 0 ? seconds - seconds_[ref] : 0;
    const bool compare = ref >= 0 && dt <= MAX_GAP_SECONDS;
    /* In whole levels: a fraction of one would lift a pixel sitting on the
       noise floor over it. */
    const double shift = compare ? round(mean - means_[ref]) : 0;
    double zone[9] = { 0 };
    double sum = 0, sx = 0, sy = 0, sxx = 0, syy = 0;

    for (int y = 0; compare && y < height_; y++)
    {
        const uint8_t *row = luma + (size_t)y * stride;
        const uint8_t *was = &frames_[ref * area + (size_t)y * width_];
        const int zy = 3 * y / height_;

        for (int x = 0; x < width_; x++)
        {
            const double d = fabs(row[x] - was[x] - shift);

            if (d <= NOISE_LEVELS)
                continue;

            /* Mirrored: the player's left is the picture's right. */
            const double px = 1 - (x + 0.5) / width_, py = (y + 0.5) / height_;

            sum += d;
            sx += d * px;
            sy += d * py;
            sxx += d * px * px;
            syy += d * py * py;
            zone[zy * 3 + 2 - 3 * x / width_] += d;
        }
    }

    const double full = HAND_SHARE * area * 255 * FULL_MOTION_PER_SECOND * dt;

    axes_[MOTION] = compare ? (float)fmin(1, sum / full) : 0;

    for (int z = 0; z < 9; z++)
        axes_[ZONE1 + z] = compare ? (float)fmin(1, zone[z] / full) : 0;

    if (axes_[MOTION] > PLACE_MOTION)
    {
        const double mx = sx / sum, my = sy / sum;

        axes_[X] = (float)mx;
        axes_[Y] = (float)my;
        /* RMS distance from the centroid, out of the most a frame allows:
           half its diagonal. */
        axes_[SPREAD] = (float)fmin(1, sqrt(fmax(0, sxx / sum - mx * mx + syy / sum - my * my))
                                       / sqrt(0.5));
    }

    newest_ = (newest_ + 1) % HISTORY;
    kept_ = kept_ < HISTORY ? kept_ + 1 : HISTORY;
    seconds_[newest_] = seconds;
    means_[newest_] = mean;

    for (int y = 0; y < height_; y++)
        memcpy(&frames_[newest_ * area + (size_t)y * width_],
               luma + (size_t)y * stride, width_);
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
