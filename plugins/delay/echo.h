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
#ifndef TH_KERNEL_DELAY_ECHO_H
#define TH_KERNEL_DELAY_ECHO_H 1

/* delay::echo's kernel (libthink/thKernel.h). The ring is the caller's,
 * as its memory is a firmware's to find: thkEchoRing() long, and zeroed
 * whenever that length changes. */

#include "thKernel.h"

/* How far `boost' may take the loop: 1.5 times what it would be, which
   `drive' has to be up to hold. */
#define ECHO_BOOST_MAX 0.5f

/* A ring of `size' samples, no more than a minute's; no ring at all for
   0, a negative size or a NaN. */
static inline unsigned int thkEchoRing (float size, float rate)
{
    const float maxSize = 60.0f * rate;

    return !(size > 0) ? 0
         : size > maxSize ? (unsigned int)maxSize
         : (unsigned int)size;
}

struct thkEcho
{
    /* Where the ring is written next, a float as the plugin's state arg
       keeps it; and the loop's filters. */
    float pos = 0;
    float lp = 0, hpIn = 0, hpOut = 0;

    float tick (float *ring, unsigned int len, float in, float behind,
                float feedback, float dry, float tone, float low, float drive,
                float boost, float rate)
    {
        if (len == 0)
            return in;

        unsigned int at = (unsigned int)(int)pos;

        if (at >= len)
            at = 0;

        /* Whole rings off a delay of two or more, keeping its sign and so
           the way the cast below rounds it, so the wrap is a step or two
           and not one per ring of a huge delay; a NaN reads the newest. */
        const float whole = (float)len;

        if (!thkFinite(behind))
            behind = 0;
        else if (fabsf(behind) >= 2 * whole)
            behind -= whole * (truncf(behind / whole) - (behind > 0 ? 1 : -1));

        int index = (int)(at - behind);

        /* A negative delay walks index forward instead of back, so clamp
           both ends rather than only wrapping negatives. */
        while (index < 0)
            index += len;
        index %= (int)len;

        const float delay = ring[index];

        /* What goes back in, changed on the way. Each stage only where its
           arg asks for it, so a ring with none of them is the ring it
           always was, to the bit. */
        float back = delay;
        const float d = thkClamp(drive, 0, 4);
        const float b = thkClamp(boost, 0, ECHO_BOOST_MAX);

        if (tone > 0 && thkFinite(tone))
        {
            const float k = 1 - expf(-2 * (float)M_PI *
                                     fminf(tone, rate * 0.45f) / rate);

            lp += k * (back - lp);
            back = lp;
        }

        if (low > 0 && thkFinite(low))
        {
            const float r = expf(-2 * (float)M_PI *
                                 fminf(low, rate * 0.45f) / rate);

            hpOut = r * (hpOut + back - hpIn);
            hpIn = back;
            back = hpOut;
        }

        if (b > 0)
            back *= 1 + b;

        /* tanh scaled so a small signal passes at unity: the drive is how
           early the curve bends, not a gain. Its ceiling is full scale
           over the drive, so a boosted loop bends at 1 at least and is
           held at full scale or under. */
        const float bend = b > 0 ? fmaxf(d, 1) : d;

        if (bend > 0)
            back = tanhf(bend * back) / bend;

        if (!thkFinite(back))
        {
            back = 0;
            lp = hpIn = hpOut = 0;
        }

        ring[at] = (feedback * back) + ((1 - feedback) * in);
        pos = (float)(at + 1);
        return ((1 - dry) * delay) + (dry * in);
    }
};

#endif /* TH_KERNEL_DELAY_ECHO_H */
