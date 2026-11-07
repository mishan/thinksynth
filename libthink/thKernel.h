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

#ifndef TH_KERNEL_H
#define TH_KERNEL_H 1

/* What a plugin's kernel may use: the arithmetic of one sample, in float,
 * with nothing of the engine's in it.
 *
 * A kernel is a plugin's work on one sample, apart from the plugin: a
 * struct of the state it keeps and a tick() from plain floats to a float.
 * The plugin runs it over a window; a patch compiled to a program inlines
 * it, so what plays live and what a microcontroller plays are the same
 * arithmetic. So a kernel takes no thArg, reads no window and includes
 * nothing of libthink's but this. Float throughout, because the FPUs of
 * the small chips a compiled patch is for are single precision, and a
 * double there is a call into software. See docs/KERNELS.md.
 */

#include <stdint.h>
#include <string.h>

/* thIsFinite's test, on the bits so -ffast-math can't fold it away. */
static inline bool thkFinite (float x)
{
    uint32_t bits;

    memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7f800000u) != 0x7f800000u;
}

/* thClampArg's: a non-finite x answers lo. */
static inline float thkClamp (float x, float lo, float hi)
{
    return !thkFinite(x) || x < lo ? lo : x > hi ? hi : x;
}

/* thBoundFreq's, in float: no slower than a wave a float's mantissa can
   step through, and no faster than Nyquist. A NaN lands on the floor. */
static inline float thkBoundFreq (float freq, float rate)
{
    const float slowest = rate / 16777216.0f;
    const float fastest = rate * 0.5f;

    return !(freq >= slowest) ? slowest : freq > fastest ? fastest : freq;
}

/* sin(2 pi x), x in turns: brought to the quarter turn either side of
   zero, where a Taylor series to t^11 is within 6e-8 of the sine, under
   what a float there can tell apart. In turns because a phase is,
   so an oscillator hands over its phase and its offset and nothing is
   multiplied by 2 pi twice. Beyond a million turns either way, a float
   has no fraction left to give and the answer is 0. */
static inline float thkSin2pi (float x)
{
    if (!(x > -1048576.0f && x < 1048576.0f))
        return 0;

    x -= (float)(int32_t)x;                 /* (-1, 1) */
    if (x > 0.5f)
        x -= 1;
    else if (x < -0.5f)
        x += 1;
    if (x > 0.25f)                          /* sin(pi - t) = sin(t) */
        x = 0.5f - x;
    else if (x < -0.25f)
        x = -0.5f - x;
    /* The crest and the trough exactly, where the series is a rounding
       short: an envelope built on half a sine peaks at its peak. */
    if (x == 0.25f || x == -0.25f)
        return x * 4;

    const float t = 6.28318530717958647692f * x, t2 = t * t;

    return t * (1 + t2 * (-1.0f / 6 + t2 * (1.0f / 120 + t2 * (-1.0f / 5040 +
                t2 * (1.0f / 362880 + t2 * (-1.0f / 39916800))))));
}

#endif /* TH_KERNEL_H */
