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

#ifndef TH_KERNEL_SIN_H
#define TH_KERNEL_SIN_H 1

/* math::sin's kernel (libthink/thKernel.h). It keeps nothing, so it is a
 * function. */

#include "thKernel.h"

/* sin(index / wavelength turns) * amp, from the kernels' sine table: the
   video patches read one or more of these a pixel, a million pixels a
   second. A 0 wavelength or amp is 1. */
static inline float thkSin (float index, float wavelength, float amp)
{
    return thkSin2pi(index / (wavelength == 0 ? 1 : wavelength)) * (amp == 0 ? 1 : amp);
}

#endif /* TH_KERNEL_SIN_H */
