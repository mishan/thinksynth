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

#ifndef THINK_SAMPLEHITS_H
#define THINK_SAMPLEHITS_H

/*
 * Where the hits in a recording start, for osc::sample to cut a file at.
 *
 * IN 5 MS HOPS, on the frames as recorded. Each hop's energy is that of the
 * first difference, which weighs a hit's attack over the body of a held
 * note. A hop is a hit when its energy is at least twice the quieter of
 * the two hops before it -- an attack straddling two hops is measured
 * against the one before it began -- and it rises more than any hop within
 * 6 either side. Measured against what is sounding just before it, so a
 * hit landing on the last one's decay counts as much as one after silence
 * -- as long as it is louder than that decay: a hit under a ring that
 * drowns it is not found, and a list of cuts written out is for that.
 *
 * The cut is where the hit's attack begins: within the hop before and the
 * hit's own, the frame where the next 1 ms of energy rises the most over
 * the 1 ms before it -- long enough that a noise burst's own flutter does
 * not outdo its attack. A hit in the first hop is cut at frame 0.
 *
 * THE SAME ANSWER IN EVERY BUILD. The frames are quantized to 16 bits and
 * the energies summed in integers; past that each step is one division, or
 * one add, of numbers converted from integers, which no compiler can fuse
 * into something that rounds differently. A cut that moved a frame between
 * the desktop and the browser would move a whole slice.
 */

#include <stdint.h>
#include <stdlib.h>

#include <algorithm>
#include <vector>

struct thSampleHit
{
    size_t frame;       /* where its slice starts, in frames as recorded */
    double strength;    /* r / (r + 4) for its rise r: 1/3 up to 1       */
    unsigned rank;      /* 0 the strongest, ties to the earlier          */
};

struct thSampleHits
{
    std::vector<thSampleHit> hits;      /* by frame */

    /* Sound before the first hit, which then also starts a slice: one
       that is not a hit and is not counted by `slices'. */
    bool leadIn = false;
};

/* Hops either side a hit has to rise above; so no two are closer. */
#define THINK_HITS_APART 6

/* Whether `hit' is used at `sensitivity' (0..1) with `slices' (0 for all). */
static inline bool thSampleHitUsed (const thSampleHit &hit, double sensitivity,
                                    int slices)
{
    return hit.strength >= 1.0 - sensitivity &&
           (slices <= 0 || hit.rank < (unsigned)slices);
}

/* `frame' of a file at `fileRate' in an engine at `rate': the first engine
   frame at or past it, which is how the reader's resample maps them. */
static inline size_t thSampleEngineFrame (size_t frame, unsigned rate,
                                          unsigned fileRate)
{
    if (fileRate == 0 || fileRate == rate)
        return frame;

    const unsigned long long n = (unsigned long long)frame * rate;

    return (size_t)((n + fileRate - 1) / fileRate);
}

/* A frame quantized, as everything below reads it. */
static inline int32_t thSampleHitQ (const float *x, size_t i)
{
    const float v = x[i] > 1 ? 1 : x[i] < -1 ? -1 : x[i];

    return (int32_t)(v * 32767.0f);
}

/* The first difference's energy at frame i. */
static inline int64_t thSampleHitD2 (const float *x, size_t i)
{
    const int64_t d = (int64_t)thSampleHitQ(x, i) -
                      (i > 0 ? thSampleHitQ(x, i - 1) : 0);

    return d * d;
}

/* Kept to a number per hop, not per frame: every file a sampler reads goes
   through here, and a six-minute one is sixteen million frames. */
static inline void thSampleFindHits (const float *x, size_t n, unsigned rate,
                                     thSampleHits &out)
{
    out.hits.clear();
    out.leadIn = false;

    const size_t hop = rate / 200;

    if (hop < 1 || n < 2 * hop)
        return;

    const size_t hops = n / hop;
    const size_t used = hops * hop;
    std::vector<int64_t> e(hops, 0);

    for (size_t h = 0; h < hops; h++)
        for (size_t i = h * hop; i < (h + 1) * hop; i++)
            e[h] += thSampleHitD2(x, i);

    /* -50 dBFS of difference, per frame: a room's noise, and the start of
       a file, rise from it by less than a hit does. */
    const int64_t floorF = (int64_t)104 * 104;
    std::vector<double> rise(hops, 0);

    for (size_t h = 0; h < hops; h++)
    {
        const int64_t before = std::min(h > 0 ? e[h - 1] : 0,
                                        h > 1 ? e[h - 2] : 0);

        rise[h] = (double)e[h] / (double)(before + floorF * (int64_t)hop);
    }

    const size_t w = std::max<size_t>(1, rate / 1000);
    std::vector<int64_t> upTo;

    for (size_t h = 0; h < hops; h++)
    {
        if (rise[h] < 2)
            continue;

        bool top = true;

        for (size_t o = h > THINK_HITS_APART ? h - THINK_HITS_APART : 0;
             o <= h + THINK_HITS_APART && o < hops && top; o++)
            if (o != h && (rise[o] > rise[h] || (rise[o] == rise[h] && o < h)))
                top = false;

        if (!top)
            continue;

        size_t cut = 0;

        if (h > 0)
        {
            /* The energy summed from `base' over the hop before, the hit's
               own and a window either side. */
            const size_t from = (h - 1) * hop;
            const size_t to = std::min((h + 1) * hop, used - w);
            const size_t base = from >= w ? from - w : 0;

            upTo.assign(to + w - base + 1, 0);

            for (size_t i = base; i < to + w; i++)
                upTo[i - base + 1] = upTo[i - base] + thSampleHitD2(x, i);

            double best = -1;

            cut = h * hop;

            for (size_t p = std::max(from, w); p <= to; p++)
            {
                const size_t k = p - base;
                const double r = (double)(upTo[k + w] - upTo[k]) /
                                 (double)(upTo[k] - upTo[k - w] +
                                          floorF * (int64_t)w);

                if (r > best)
                {
                    best = r;
                    cut = p;
                }
            }
        }

        out.hits.push_back({ cut, rise[h] / (rise[h] + 4), 0 });
    }

    std::vector<size_t> order(out.hits.size());

    for (size_t i = 0; i < order.size(); i++)
        order[i] = i;

    std::stable_sort(order.begin(), order.end(), [&out] (size_t a, size_t b)
                     { return out.hits[a].strength > out.hits[b].strength; });

    for (size_t r = 0; r < order.size(); r++)
        out.hits[order[r]].rank = (unsigned)r;

    /* Sound before the first hit, more than the gap apart from it. */
    if (!out.hits.empty() && out.hits[0].frame > THINK_HITS_APART * hop)
        for (size_t i = 0; i < out.hits[0].frame && !out.leadIn; i++)
            out.leadIn = abs(thSampleHitQ(x, i)) > 327;     /* -40 dBFS */
}

#endif /* THINK_SAMPLEHITS_H */
