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

#ifndef TH_KERNEL_MIDI2FREQ_H
#define TH_KERNEL_MIDI2FREQ_H 1

/* misc::midi2freq's kernel (libthink/thKernel.h): a note's frequency, at
 * most just short of Nyquist. It keeps nothing, so it is a function. */

#include <math.h>

#include "thKernel.h"

/* Just short of Nyquist, not on it: a wavelength of exactly two samples
   would land an oscillator on its pulse width's boundary. Clamped as a
   float may be: a note past float range is an infinity, which is more
   than the top, and a NaN is less than nothing. See midi2freq.cpp. */
static inline float thkMidi2freq (float note, float rate)
{
    const float top = rate * 0.5f - 1;
    const float n = note - 69;

    if (!(n > -2000) || !(n < 2000))        /* past float range, or NaN */
        return n > 0 ? top : 0;

    /* Whole octaves exactly, as a power of two, and exp2f over only the
       twelfth of the rest: within a rounding of what the double did,
       where exp2f of the whole would be a unit or two out, and a unit
       out is an oscillator wrapping a sample early. */
    const int octaves = (int)floorf(n * (1.0f / 12));
    const float hz = ldexpf(440 * exp2f((n - 12 * octaves) * (1.0f / 12)),
                            octaves);

    return hz > top ? top : hz;
}

#endif /* TH_KERNEL_MIDI2FREQ_H */
