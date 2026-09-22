/*
 * Copyright (C) 2004-2026 Metaphonic Labs
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

#ifndef THINK_SHIFTER_H
#define THINK_SHIFTER_H

/* The two crossfaded read heads delay::pitchshift is, and delay::fdn runs
 * in its shimmer path. The head of plugins/delay/pitchshift.cpp says why
 * they are shaped this way; this is the arithmetic, shared so that the two
 * nodes cannot come to disagree about it.
 *
 * The caller owns the ring and writes the newest sample to `at' before
 * reading, and owns the phase, which it steps with shifterStep().
 */

#include <math.h>

/* The ring `back' samples behind the sample just written at `at', read
 * between samples by the four-point Hermite cubic. A point the cubic wants
 * from ahead of the write head is not there yet, and is the newest sample
 * instead: it is only ever asked for within a sample of a head's wrap,
 * where that head's gain is next to nothing. `back' is at most the ring's
 * length less three.
 */
static inline float shifterRead (const float *ring, unsigned int len,
                                 unsigned int at, double back)
{
    const unsigned int whole = (unsigned int)back;
    const float f = (float)(back - whole);
    const unsigned int ahead = whole == 0 ? 0 : whole - 1;
    const float xm = ring[(at + len - ahead) % len];
    const float x0 = ring[(at + len - whole) % len];
    const float x1 = ring[(at + len - whole - 1) % len];
    const float x2 = ring[(at + len - whole - 2) % len];
    const float c1 = 0.5f * (x1 - xm);
    const float c2 = xm - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm) + 1.5f * (x0 - x1);

    return ((c3 * f + c2) * f + c1) * f + x0;
}

/* Both heads, crossfaded: one `phase' windows back, the other half a
 * window further, their raised-cosine gains summing to one and each zero
 * where its head wraps. Exactly one and exactly zero at a phase of zero,
 * which is where the heads stay at a ratio of one.
 */
static inline float shifterHeads (const float *ring, unsigned int len,
                                  unsigned int at, float phase, float window)
{
    const float ga = 0.5f - 0.5f * (float)cos(2.0 * M_PI * phase);
    float pb = phase + 0.5f;

    if (pb >= 1.0f)
        pb -= 1.0f;

    return ga * shifterRead(ring, len, at, (double)phase * window) +
           (1 - ga) * shifterRead(ring, len, at, (double)pb * window);
}

/* The heads' phase one sample on: they fall behind the write head by
 * (1 - ratio) samples a sample, as a fraction of the window. In float and
 * stepped in float, for delay::chorus's reason -- a window boundary is not
 * an event.
 */
static inline float shifterStep (float phase, float ratio, float window)
{
    phase += (1 - ratio) / window;

    if (phase >= 1.0f || phase < 0.0f)
        phase -= floorf(phase);

    return phase;
}

#endif
