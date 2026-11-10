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

#include <cmath>

/* thIsFinite's test, on the bits so -ffast-math can't fold it away: and
   past an empty asm, since clang's takes a float argument to be finite
   and folds even the bits' test to true. */
static inline bool thkFinite (float x)
{
    uint32_t bits;

    memcpy(&bits, &x, sizeof(bits));
#if defined(__GNUC__)
    __asm__("" : "+r"(bits));
#endif
    return (bits & 0x7f800000u) != 0x7f800000u;
}

/* x, which the compiler can't see: a compiled graph's numbers, where a
   kernel's libm call would otherwise be worked out at build time, which
   can be an ulp from the library's answer at run time, the plugin's. */
static inline float thkHeld (float x)
{
    volatile float held = x;

    return held;
}

/* thClampArg's: a non-finite x answers lo. */
static inline float thkClamp (float x, float lo, float hi)
{
    return !thkFinite(x) || x < lo ? lo : x > hi ? hi : x;
}

/* thClampMag's: |x| held to hi, a non-finite x answering 0. */
static inline float thkClampMag (float x, float hi)
{
    return !thkFinite(x) ? 0 : x > hi ? hi : x < -hi ? -hi : x;
}

/* thBoundFreq's, in float: no slower than a wave a float's mantissa can
   step through, and no faster than Nyquist. A NaN lands on the floor. */
static inline float thkBoundFreq (float freq, float rate)
{
    const float slowest = rate / 16777216.0f;
    const float fastest = rate * 0.5f;

    /* thkFinite, not a comparison: -ffast-math takes NaN to compare as
       anything at all. */
    return !thkFinite(freq) || freq < slowest ? slowest
           : freq > fastest ? fastest : freq;
}

/* sin(2 pi x), x in turns, from a table of 2048 to the turn and a line
   between neighbors: within 1.2e-6 of the sine (118 dB down), and a
   table read and one multiply-add, where a series is a dozen in a row --
   on a small FPU each waiting on the last. Exact at 0, the crest and the
   trough, which fall on the table: an envelope built on half a sine
   peaks at its peak. Beyond a million turns either way, a float has no
   fraction left to give and the answer is 0. */
#define THK_SINE_SIZE 2048

struct thkSineTable
{
    float at[THK_SINE_SIZE + 1];        /* the last is the first again */

    thkSineTable ()
    {
        for (int i = 0; i <= THK_SINE_SIZE; i++)
            at[i] = (float)std::sin(6.283185307179586476925 * i / THK_SINE_SIZE);
        /* The crest and the trough exactly, whatever sin() rounds them to. */
        at[THK_SINE_SIZE / 4] = 1;
        at[THK_SINE_SIZE * 3 / 4] = -1;
        at[0] = at[THK_SINE_SIZE / 2] = at[THK_SINE_SIZE] = 0;
    }
};

/* One table for every kernel that includes this, made before main. */
inline const thkSineTable thkSine;

static inline float thkSin2pi (float x)
{
    if (!thkFinite(x) || !(x > -1048576.0f && x < 1048576.0f))
        return 0;

    x -= (float)(int32_t)x;                 /* (-1, 1) */
    if (x < 0)
        x += 1;
    /* A hair under 0 and a turn on is a whole turn in float: the table's
       end, one past which there is nothing. */
    if (x >= 1)
        x = 0;

    const float where = x * THK_SINE_SIZE;
    const int i = (int)where;
    const float *t = thkSine.at + i;

    return t[0] + (where - (float)i) * (t[1] - t[0]);
}

#endif /* TH_KERNEL_H */
