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

#ifndef TH_KERNEL_NOISE_H
#define TH_KERNEL_NOISE_H 1

/* osc::noise's kernel (libthink/thKernel.h): white, pink or brown, a draw
 * a sample whatever the color. The draw is plugins/dice.h's 48-bit
 * generator, kept as the integer it is, and turned to a float as exactly as
 * the double the plugin used to go through: the same stream, bit for bit.
 * See noise.cpp for the colors and the levels. */

#include <stdint.h>

#include "thKernel.h"

/* Kellett's economical pink filter: three one-pole sections summed, plus a
   little of the white itself for the top octave. */
#define PINK_P0 0.99765f
#define PINK_P1 0.96300f
#define PINK_P2 0.57000f
#define PINK_G0 0.0990460f
#define PINK_G1 0.2965164f
#define PINK_G2 1.0526913f
#define PINK_GW 0.1848f

/* Its output's standard deviation for a white input uniform over -1..1,
   measured over twenty million samples: 1.720370, against the white's own
   0.577270. Analytically it is a sum of correlated terms and not worth
   writing out; what matters is that it is a constant of the coefficients
   above, so it moves only if they do. */
#define PINK_SIGMA 1.720370f

/* The leaky integrator's pole. A corner at rate*(1 - p)/2pi -- 35 Hz at
   44.1k -- below which the slope flattens to nothing rather than running
   away to DC, and above which it is the 6 dB an octave that makes it brown.
   Its standard deviation is exact: sqrt((1 - p)/(1 + p)/3) for a white input
   uniform over -1..1, which is 0.0289044. */
#define BROWN_P 0.995f
#define BROWN_SIGMA 0.0289044f

/* Where `amp' sits on a color with no peak of its own. Five sigma is the
   largest excursion in twenty million samples of either; a Gaussian is past
   it one sample in three and a half million. */
#define NOISE_SIGMAS 5.0f

struct thkNoise
{
    struct Ctl
    {
        int   color;                    /* 0 white, 1 pink, 2 brown */
        float amp;                      /* a peak */
    };

    static Ctl ctl (float color, float amp)
    {
        amp = thkClamp(amp, 0, 1);
        return { (int)thkClamp(color, 0, 2), amp == 0 ? 1 : amp };
    }

    uint64_t x = 0;                     /* the generator's 48 bits */
    float b0 = 0, b1 = 0, b2 = 0, brown = 0;

    /* plugins/dice.h's thDiceSeedBits. */
    void seed (uint32_t bits) { x = ((uint64_t)bits << 16) | 0x330Eu; }

    float tick (const Ctl &c)
    {
        x = (x * 0x5DEECE66Dull + 0xBull) & ((((uint64_t)1) << 48) - 1);

        /* 2 x / 2^48 - 1, which a double holds exactly, rounded once to a
           float: the same as the 48-bit integer less half its range,
           rounded once and halved 47 times. */
        const float w = (float)((int64_t)x - ((int64_t)1 << 47)) * (1.0f / 140737488355328.0f);
        float y;

        /* Both filters run every sample whatever the color, so moving it
           finds them settled. */
        b0 = PINK_P0 * b0 + w * PINK_G0;
        b1 = PINK_P1 * b1 + w * PINK_G1;
        b2 = PINK_P2 * b2 + w * PINK_G2;
        brown = BROWN_P * brown + (1.0f - BROWN_P) * w;
        if (c.color == 1)
            y = (b0 + b1 + b2 + w * PINK_GW) / (PINK_SIGMA * NOISE_SIGMAS);
        else if (c.color == 2)
            y = brown / (BROWN_SIGMA * NOISE_SIGMAS);
        else
            y = w;
        y *= c.amp;
        return y > c.amp ? c.amp : y < -c.amp ? -c.amp : y;
    }
};

#endif /* TH_KERNEL_NOISE_H */
