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

#ifndef TH_KERNEL_SVF_H
#define TH_KERNEL_SVF_H 1

/* filt::svf's kernel (libthink/thKernel.h): one sample of the trapezoidal
 * state-variable filter, its low, band and high outputs. See svf.cpp. */

#include <math.h>

#include "thKernel.h"

struct thkSvf
{
    /* The resonance's ceiling and the cutoff's, a share of the rate; see
       svf.cpp for why these. */
    static constexpr float RESMAX = 0.99f, CUTOFF_CEIL = 0.49f;

    /* What the cutoff and resonance make of the filter. tanf, once while
       they hold still, which is nearly always. The float coefficients
       detune the resonance a little near Nyquist at high res. */
    struct Ctl
    {
        float k, a1, a2, a3;
    };

    static Ctl ctl (float cut, float res, float rate)
    {
        const float g = tanf(3.14159265358979323846f * thkClamp(cut, 0, CUTOFF_CEIL * rate) /
                             rate);
        const float k = 2 - 2 * thkClamp(res, 0, RESMAX);
        const float d = 1 / (1 + g * (g + k));

        return { k, d, g * d, g * g * d };
    }

    float ic1 = 0, ic2 = 0;

    static float clip (float x) { return x > 1 ? 1 : x < -1 ? -1 : x; }

    /* A state under this is no sound, and not a denormal to crawl through. */
    static float flush (float x) { return fabsf(x) < 1e-30f ? 0 : x; }

    float tick (const Ctl &c, float in, float &band, float &high)
    {
        const float v0 = thkClamp(in, -1, 1);
        const float v3 = v0 - ic2;
        const float v1 = c.a1 * ic1 + c.a2 * v3;
        const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;

        ic1 = flush(2 * v1 - ic1);
        ic2 = flush(2 * v2 - ic2);
        band = clip(c.k * v1);
        high = clip(v0 - c.k * v1 - v2);
        return clip(v2);
    }
};

#endif /* TH_KERNEL_SVF_H */
