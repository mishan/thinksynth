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

#ifndef TH_KERNEL_SIMPLE_H
#define TH_KERNEL_SIMPLE_H 1

/* osc::simple's kernel (libthink/thKernel.h): one sample of the oscillator
 * and whether a cycle began on it. Its phase counts samples, not turns;
 * see simple.cpp for why the wrap is as it is. Levels are full scale. */

#include <math.h>
#include <string.h>

#include "thKernel.h"

struct thkSimple
{
    /* What the frequency, multiplier, level, pulse width and waveform
       make of a cycle, once while they hold still. */
    struct Ctl
    {
        float wavelength;               /* samples */
        float halfwave;                 /* the first part's, pw of it */
        float perHalf, perRest;         /* 1 / (2 halfwave), 0.5 / the rest */
        float hi, lo, range;            /* the levels */
        int   waveform;
    };

    static Ctl ctl (float freq, float mul, float amp, float pw,
                    float waveform, float rate)
    {
        Ctl c;

        if (mul)
            freq *= mul;
        c.wavelength = rate / thkBoundFreq(freq, rate);
        c.hi = amp == 0 ? 1 : amp;
        c.lo = -c.hi;
        c.range = c.hi - c.lo;
        /* Neither part of the cycle may be empty: each divides by its
           length. */
        c.halfwave = c.wavelength * thkClamp(pw == 0 ? 0.5f : pw, 0.001f, 0.999f);
        c.perHalf = 1 / (2 * c.halfwave);
        c.perRest = 0.5f / (c.wavelength - c.halfwave);
        c.waveform = thkFinite(waveform) && waveform >= 0 && waveform < 6
                     ? (int)waveform : 0;
        return c;
    }

    float position = 0;                 /* samples into the cycle */

    float tick (const Ctl &c, float fm, float fmamt, float reset, float &sync)
    {
        position += 1 + fm * fmamt;
        sync = 0;
        /* Only a wrap says a cycle began, a reset's own included, as the
           plugin always has. */
        if (reset == 1)
            position = 0;
        /* One subtraction, as a cycle's end is passed by under a sample;
           fmodf for a wavelength shortened under the phase by a retune. */
        if (position > c.wavelength)
        {
            position -= c.wavelength;
            if (position > c.wavelength)
                position = fmodf(position, c.wavelength);
            sync = 1;
        }
        else if (position < 0)
        {
            position += c.wavelength;
            if (position < 0)
                position = fmodf(position, c.wavelength) + c.wavelength;
            sync = 1;
        }

        const float ratio = position < c.halfwave
                            ? position * c.perHalf
                            : (position - c.halfwave) * c.perRest + 0.5f;

        switch (c.waveform)
        {
        case 1:                         /* sawtooth */
            return c.range * ratio + c.lo;
        case 2:                         /* square */
        {
            /* The level with the sign of (ratio - 0.5) OR'd in: an OR,
               which a negative level keeps negative. See simple.cpp. */
            const float half = ratio - 0.5f;
            uint32_t level, sign;
            float y;

            memcpy(&level, &c.hi, sizeof(level));
            memcpy(&sign, &half, sizeof(sign));
            level |= sign & 0x80000000u;
            memcpy(&y, &level, sizeof(y));
            return y;
        }
        case 3:                         /* triangle */
            return ratio < 0.5f ? c.range * (ratio * 2) + c.lo
                                : -c.range * (ratio * 2 - 1) + c.hi;
        case 4:                         /* half-circle */
        {
            const float w = c.wavelength, from = ratio < 0.5f ? 0 : c.halfwave;
            const float p = position - from;

            return 2.82842712474619f * sqrtf((w - 2 * p) * p / (w * w)) *
                   (ratio < 0.5f ? c.hi : c.lo);
        }
        case 5:                         /* parabola */
            return ratio < 0.5f
                   ? c.hi * (1 - (ratio * 4 - 1) * (ratio * 4 - 1))
                   : c.hi * (((ratio - 0.5f) * 4 - 1) * ((ratio - 0.5f) * 4 - 1) - 1);
        default:                        /* sine */
            return c.hi * thkSin2pi(ratio);
        }
    }
};

#endif /* TH_KERNEL_SIMPLE_H */
