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
#ifndef TH_KERNEL_ALLPASS_H
#define TH_KERNEL_ALLPASS_H 1

/* delay::allpass's kernel (libthink/thKernel.h). The line is the
 * caller's, as its memory is a firmware's to find: thkAllpassLen() long,
 * and zeroed whenever that length changes. */

#include "thKernel.h"

#define ALLPASS_GAIN_MAX 0.999f

/* A line of no samples is not a delay, and one longer than a second is a
   number nobody wrote on purpose. Both land on the nearest thing that is:
   no line at all, and the input. */
static inline unsigned int thkAllpassLen (float delay, float rate)
{
    return (unsigned int)thkClamp(delay, 0, rate);
}

struct thkAllpass
{
    /* Where the line is read and written next. A float, as the plugin's
       state arg keeps it. */
    float pos = 0;

    float tick (float *line, unsigned int len, float in, float gain)
    {
        if (len == 0)
            return in;

        const float g = thkClampMag(gain, ALLPASS_GAIN_MAX);
        unsigned int at = (unsigned int)pos;

        /* The length can have changed under it, so the position is bounded
           here rather than trusted. */
        if (at >= len)
            at = 0;

        const float delayed = line[at];
        float v = in + g * delayed;

        /* One non-finite sample would otherwise be fed back for ever: the
           line is the state, so what goes into it has to be a number that
           can come out again. */
        if (!thkFinite(v))
            v = 0;

        line[at] = v;
        pos = (float)((at + 1) % len);
        return delayed - g * v;
    }
};

#endif /* TH_KERNEL_ALLPASS_H */
