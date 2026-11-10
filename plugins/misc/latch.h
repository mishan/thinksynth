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
#ifndef TH_KERNEL_MISC_LATCH_H
#define TH_KERNEL_MISC_LATCH_H 1

/* misc::latch's kernel (libthink/thKernel.h): sample and hold. */

#include "thKernel.h"

struct thkLatch
{
    float held = 0;

    /* `in' taken while `latch' is above 0, and held while it isn't. */
    float tick (float in, float latch)
    {
        if (latch > 0)
            held = in;
        return held;
    }
};

#endif /* TH_KERNEL_MISC_LATCH_H */
