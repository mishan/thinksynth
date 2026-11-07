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

#ifndef TH_KERNEL_FMOP_H
#define TH_KERNEL_FMOP_H 1

/* osc::fmop's kernel: one sample of the operator (libthink/thKernel.h).
 * See fmop.cpp for what the operator is and why it is built so. Signals
 * are full scale (TH_MAX is 1); index and feedback are radians. */

#include "thKernel.h"

/* Radians: what feedback is clamped to, where y = sin(x + b*y) stops
   having a solution. See fmop.cpp. */
#define FMOP_FEEDBACK_MAX 1.0f

struct thkFmop
{
    float phase = 0;            /* turns, [0, 1) */
    float y1 = 0, y2 = 0;       /* the last two outputs */

    float tick (float freq, float ratio, float mod, float index,
                float feedback, float reset, float rate)
    {
        ratio = thkFinite(ratio) ? ratio : 1;
        /* The floor and the NaN, not the ceiling: a negative index is the
           same timbre with the modulator inverted, best read as none. */
        index = thkFinite(index) && index > 0 ? index : 0;
        feedback = thkClamp(feedback, 0, FMOP_FEEDBACK_MAX);
        mod = thkFinite(mod) ? mod : 0;

        if (reset > 0)
            phase = 0;

        /* The last two outputs averaged go round the loop: a null at
           Nyquist, where a one-sample loop would ring. */
        const float y = thkSin2pi(phase + (index * mod +
                                           feedback * (y1 + y2) * 0.5f) *
                                          (1 / 6.28318530717958647692f));

        y2 = y1;
        y1 = y;
        phase += thkBoundFreq(freq * ratio, rate) / rate;
        /* One end only: the increment is positive. */
        if (phase >= 1)
            phase -= (float)(int32_t)phase;
        return y;
    }
};

#endif /* TH_KERNEL_FMOP_H */
