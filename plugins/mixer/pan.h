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

#ifndef TH_KERNEL_PAN_H
#define TH_KERNEL_PAN_H 1

/* mixer::pan's kernel (libthink/thKernel.h): one signal to two sides by
 * constant power. It keeps nothing; what the pan makes of the sides is
 * its Ctl. See pan.cpp. */

#include <math.h>

#include "thKernel.h"

struct thkPan
{
    struct Ctl
    {
        float left, right;      /* cos and sin of (pan + 1) pi / 4 */
    };

    /* pan held to -1..1, a non-finite one the middle. sinf and cosf, not
       the table: the sides' powers must sum to the input's, and the
       table's rounding on each is more than that law allows; once a
       window, or a compiled voice's call, the cost doesn't show. */
    static Ctl ctl (float pan)
    {
        const float theta = (thkClampMag(pan, 1) + 1) * 0.785398163397448310f;

        return { cosf(theta), sinf(theta) };
    }
};

#endif /* TH_KERNEL_PAN_H */
