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

#ifndef TH_KERNEL_EQ_H
#define TH_KERNEL_EQ_H 1

/* filt::eq's kernel (libthink/thKernel.h): one sample of a peak or shelf
 * band, filt::svf's integrators with the output mixed from the input and
 * their two taps. See eq.cpp. */

#include <math.h>

#include "thKernel.h"

struct thkEq
{
    enum { PEAK, LOWSHELF, HIGHSHELF };

    /* The frequency's floor in hertz and ceiling as a share of the rate,
       the gain's reach in dB, and q's range, a shelf's shorter; see
       eq.cpp for why these. */
    static constexpr float FLOOR = 10, CEIL = 0.49f, GAINMAX = 24,
                           QMIN = 0.1f, QMAX = 18, SHELFQMAX = 2;

    struct Ctl
    {
        float a1, a2, a3, m0, m1, m2;
    };

    /* tanf, exp2f and sqrtf, once while the args hold still. Past either
       end of its range an arg is that end, and a type a Peak; not a
       number, a Peak, a freq of FLOOR, 0 dB and a q of QMIN. */
    static Ctl ctl (float type, float freq, float gain, float q, float rate)
    {
        const int t = thkFinite(type) && type >= 0 && type < 3 ? (int)type : PEAK;
        const float A = exp2f(thkClampMag(gain, GAINMAX) * 0.08304820237218406f);
        const float qq = thkClamp(q, QMIN, t == PEAK ? QMAX : SHELFQMAX);
        float g = tanf(3.14159265358979323846f * thkClamp(freq, FLOOR, CEIL * rate) / rate);
        float k = 1 / qq, m0 = 1, m1, m2;

        if (t == PEAK)
        {
            k = 1 / (qq * A);
            m1 = k * (A * A - 1);
            m2 = 0;
        }
        else if (t == LOWSHELF)
        {
            g /= sqrtf(A);
            m1 = k * (A - 1);
            m2 = A * A - 1;
        }
        else
        {
            g *= sqrtf(A);
            m0 = A * A;
            m1 = k * (1 - A) * A;
            m2 = 1 - A * A;
        }

        const float d = 1 / (1 + g * (g + k));

        return { d, g * d, g * g * d, m0, m1, m2 };
    }

    float ic1 = 0, ic2 = 0;

    /* A state under this is no sound, and not a denormal to crawl through. */
    static float flush (float x) { return fabsf(x) < 1e-30f ? 0 : x; }

    float tick (const Ctl &c, float in)
    {
        /* Non-finite is 0, and a finite input past any real signal cannot
           carry the state to infinity. */
        const float v0 = thkClampMag(in, 1e6f);
        const float v3 = v0 - ic2;
        const float v1 = c.a1 * ic1 + c.a2 * v3;
        const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;

        ic1 = flush(2 * v1 - ic1);
        ic2 = flush(2 * v2 - ic2);
        return c.m0 * v0 + c.m1 * v1 + c.m2 * v2;
    }
};

#endif /* TH_KERNEL_EQ_H */
