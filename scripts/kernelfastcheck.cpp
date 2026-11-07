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

/*
 * kernelfastcheck -- the kernels' guards against a NaN, built with
 * -ffast-math (THINK_FAST_MATH), which takes a comparison with NaN to come
 * out however suits it: a guard written as one reads a table at (int)NaN.
 */

#include <cstdio>
#include <cstring>

#include "thKernel.h"
#include "../plugins/env/adsr.h"

static int failed = 0;

static void
report (const char *name, bool ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", name);
    failed += !ok;
}

int
main ()
{
    /* From its bits, so the compiler can't see it is a NaN. */
    volatile uint32_t bits = 0x7fc00000u;
    uint32_t b = bits;
    float nan;

    memcpy(&nan, &b, sizeof(nan));
    report("a sine of NaN is 0, not a read past the table", thkSin2pi(nan) == 0);
    /* What it gives back is fast math's to say; past the table is a crash. */
    (void)thkLn1e(nan);
    report("the envelopes' log of NaN reads no table", true);
    report("a NaN frequency is the slowest", thkBoundFreq(nan, 32000) == 32000 / 16777216.0f);

    printf("%d failure%s\n", failed, failed == 1 ? "" : "s");
    return failed ? 1 : 0;
}
