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

#include <algorithm>

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

/* The bin holding the middle of `count' values. */
static int median (const int *bins, int n, long count)
{
    long seen = 0;

    for (int i = 0; i < n; i++)
        if ((seen += bins[i]) * 2 > count)
            return i;

    return n - 1;
}

/* A frame's white: where it clips, if enough of it sits within a couple of
   levels of its brightest -- JPEG blurs a clipped patch by a level or two
   -- and that is near the top, as a limited range's 235 or a full range's
   255 is; past any level otherwise. */
static int whiteOf (const int *levels, size_t area)
{
    int top = 255;

    while (top > 0 && levels[top] == 0)
        top--;

    if (top < 224)
        return 256;

    const size_t near = levels[top] + levels[top - 1] + levels[top - 2];

    return near * 100 >= area ? top - 2 : 256;
}

void thMotion::feed (const uint8_t *luma, int stride, double seconds)
{
    if (width_ < 3 || height_ < 3)
        return;

    const size_t area = (size_t)width_ * height_;
    int levels[256] = { 0 };
    double total = 0;
    bool repeat = kept_ > 0;

    for (int y = 0; y < height_; y++)
    {
        const uint8_t *row = luma + (size_t)y * stride;

        for (int x = 0; x < width_; x++)
        {
            levels[row[x]]++;
            total += row[x];
        }
        repeat = repeat && !memcmp(row, &frames_[newest_ * area + (size_t)y * width_], width_);
    }

    axes_[BRIGHT] = (float)(total / area / 255);

    /* A frame seen before -- a browser drawing faster than its camera --
       says nothing new, and is not kept, so every comparison is between
       two of the camera's own frames. Past MAX_GAP_SECONDS the feed has
       frozen, and goes still. */
    if (repeat)
    {
        if (seconds - seconds_[newest_] > MAX_GAP_SECONDS)
        {
            axes_[MOTION] = 0;
            std::fill(axes_ + ZONE1, axes_ + AXES, 0.0f);
        }

        return;
    }

    const int white = whiteOf(levels, area);
    int ref = -1;

    for (int k = 0; k < kept_ && ref < 0; k++)
    {
        const int i = (newest_ - k + HISTORY) % HISTORY;

        if (seconds - seconds_[i] >= COMPARE_SECONDS)
            ref = i;
    }

    const double dt = ref >= 0 ? seconds - seconds_[ref] : 0;
    const bool compare = ref >= 0 && dt <= MAX_GAP_SECONDS;
    const uint8_t *was = compare ? &frames_[ref * area] : NULL;
    const int wasWhite = compare ? whites_[ref] : 256;
    double gain = 1;
    int noiseFloor = NOISE_LEVELS;

    /* What each pixel is predicted to be from the one it is compared with,
       and how far it is from that. A pixel clipped then may truly have been
       brighter, so only falling below the prediction says anything about
       it; and nothing is predicted past this frame's own white. */
    auto residual = [&] (int now, int then) {
        const double d = now - std::min(gain * then, (double)white);

        return then >= wasWhite && d > 0 ? 0 : fabs(d);
    };

    if (compare)
    {
        /* Exposure is a gain: the median ratio of each pixel to the one it
           is compared with, in RATIO_STEPS a unit, from pixels neither too
           dark for a ratio to mean anything nor clipped in either frame. A
           median, so a hand covering less than half of them does not move
           it. A pixel clipped in both frames says nothing about exposure,
           and nothing about noise. */
        static const int RATIO_STEPS = 128, RATIO_BINS = 4 * RATIO_STEPS;
        int ratios[RATIO_BINS] = { 0 }, unclipped[RATIO_BINS] = { 0 };
        long counted = 0, countedUnclipped = 0;

        for (int y = 0; y < height_; y++)
            for (int x = 0; x < width_; x++)
            {
                const int now = luma[(size_t)y * stride + x], then = was[(size_t)y * width_ + x];
                const bool clippedThen = then >= wasWhite, clippedNow = now >= white;

                if (then < 16 || (clippedThen && clippedNow))
                    continue;

                const int bin = std::min(RATIO_BINS - 1, now * RATIO_STEPS / then);

                ratios[bin]++;
                counted++;
                if (!clippedThen && !clippedNow)
                {
                    unclipped[bin]++;
                    countedUnclipped++;
                }
            }

        /* A flat bright wall filling the picture looks clipped and may not
           be: with too little left once it is set aside, the pixels clipped
           in only one frame too. */
        if (countedUnclipped * 16 >= (long)area)
            gain = (median(unclipped, RATIO_BINS, countedUnclipped) + 0.5) / RATIO_STEPS;
        else if (counted * 16 >= (long)area)
            gain = (median(ratios, RATIO_BINS, counted) + 0.5) / RATIO_STEPS;

        /* The noise: the median residual is 0.6745 of a Gaussian's sigma,
           and 4 sigma passes one pixel in sixteen thousand -- from enough
           pixels that a hand in them is not most of them. */
        int residuals[256] = { 0 };
        long informative = 0;

        for (int y = 0; y < height_; y++)
            for (int x = 0; x < width_; x++)
            {
                const int now = luma[(size_t)y * stride + x], then = was[(size_t)y * width_ + x];

                if (then >= wasWhite && now >= white)
                    continue;

                residuals[std::min(255, (int)residual(now, then))]++;
                informative++;
            }

        if (informative * 16 >= (long)area)
            noiseFloor = std::max(noiseFloor,
                                  (int)(4 / 0.6745 * (median(residuals, 256, informative) + 0.5)));
    }

    double zone[9] = { 0 };
    double sum = 0, sx = 0, sy = 0, sxx = 0, syy = 0;

    for (int y = 0; compare && y < height_; y++)
    {
        const uint8_t *row = luma + (size_t)y * stride;
        const uint8_t *then = was + (size_t)y * width_;
        const int zy = 3 * y / height_;

        for (int x = 0; x < width_; x++)
        {
            const double d = residual(row[x], then[x]);

            if (d <= noiseFloor)
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
    whites_[newest_] = white;

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
