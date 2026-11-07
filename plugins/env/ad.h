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

#ifndef TH_KERNEL_AD_H
#define TH_KERNEL_AD_H 1

/* env::ad's kernel: one sample of the envelope, and whether the note still
 * sounds (libthink/thKernel.h). Times are samples; levels full scale. */

#include "thKernel.h"

struct thkAd
{
    enum { ATTACK, DECAY, DONE };

    float position = 0;
    int   phase = ATTACK;

    float tick (float a, float d, float p, float reset, float &play)
    {
        const float peak = p == 0 ? 1 : p;

        if (reset > 0)
        {
            position = 0;
            phase = a == 0 ? DECAY : ATTACK;
        }
        /* A segment of no samples completes at once rather than dividing
           by its own length. */
        if (phase == ATTACK && a <= 0)
        {
            phase = DECAY;
            position = 0;
        }
        if (phase == DECAY && d <= 0)
        {
            phase = DONE;
            position = 0;
        }

        /* Each half is half a sine: up from the trough, down from the
           crest. */
        float out = 0;

        play = phase != DONE;
        if (phase == ATTACK)
        {
            out = (thkSin2pi((position++ / a - 0.5f) * 0.5f) + 1) * (peak * 0.5f);
            if (position >= a)
            {
                phase = DECAY;
                position = 0;
            }
        }
        else if (phase == DECAY)
        {
            out = (thkSin2pi((position++ / d + 0.5f) * 0.5f) + 1) * (peak * 0.5f);
            if (position >= d)
                phase = DONE;
        }
        else if (reset > 0)
        {
            position = 0;
            phase = ATTACK;
        }
        return out;
    }
};

#endif /* TH_KERNEL_AD_H */
