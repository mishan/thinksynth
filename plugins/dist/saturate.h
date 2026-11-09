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

#ifndef TH_KERNEL_SATURATE_H
#define TH_KERNEL_SATURATE_H 1

/* dist::saturate's kernel (libthink/thKernel.h): tanh of the input by a
 * factor. It keeps nothing, so it is a function. */

#include <math.h>

#include "thKernel.h"

/* tanh in float: (e^2x - 1) / (e^2x + 1), on |x| and its sign put back,
   with expm1f for e^2x - 1, which keeps the small end that 1 - 2 / (e^2x +
   1) cancels away. Past 9 a float can't tell it from 1, and expm1f is
   short of overflowing. */
static inline float thkTanh (float x)
{
    const float a = fabsf(x);
    const float e = expm1f(2 * a);
    const float t = a > 9 ? 1 : e / (e + 2);

    return x < 0 ? -t : t;
}

static inline float thkSaturate (float in, float factor)
{
    return thkTanh(factor * in);
}

#endif /* TH_KERNEL_SATURATE_H */
