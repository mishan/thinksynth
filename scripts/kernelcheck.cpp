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
 * kernelcheck -- the plugins' kernels against the arithmetic they stand
 * for (libthink/thKernel.h, docs/KERNELS.md).
 *
 * A kernel is float and approximate where the plugin it came from was
 * double and exact, so what is held down is how close: each against a
 * double reference of the same formula, over inputs a patch could send.
 */

#include <cmath>
#include <cstdio>
#include <string>

#include "thKernel.h"
#include "../plugins/osc/fmop.h"

static int failed = 0;

static void
report (const char *name, double worst, double limit)
{
    if (worst <= limit)
        printf("ok    %s (worst %.2g)\n", name, worst);
    else
    {
        printf("FAIL  %s\n  worst %.3g, over %.3g\n", name, worst, limit);
        failed++;
    }
}

int
main ()
{
    double worst = 0;

    /* Every 1/4096 of a turn, over a hundred turns either way. */
    for (int i = -409600; i <= 409600; i++)
    {
        /* Against the angle it was handed: a float at a hundred turns
           can't hold one finer than its own step. */
        const float x = (float)(i / 4096.0 + 1e-4);

        worst = std::max(worst, std::fabs(thkSin2pi(x) - std::sin(2 * M_PI * (double)x)));
    }
    report("the sine, over a hundred turns either way", worst, 1e-6);

    const float odd[] = { NAN, INFINITY, -INFINITY, 1e7f, -1e7f };
    worst = 0;
    for (float x : odd)
        worst = std::max(worst, (double)std::fabs(thkSin2pi(x)));
    report("a sine of nothing a float can turn is 0", worst, 0);

    /* The operator against y = sin(2 pi phase + index mod + fb avg(y)) in
       double, its phase stepped in float as the plugin always has: so a
       window boundary is no event (fmop.cpp). */
    thkFmop op;
    float phase = 0;
    double y1 = 0, y2 = 0;

    worst = 0;
    for (int n = 0; n < 32000; n++)
    {
        const double mod = std::sin(n * 0.013), index = 2 + std::sin(n * 0.0007);
        const double fb = 0.3, freq = 220, ratio = 1.5, rate = 32000;
        const double want = std::sin(2 * M_PI * phase + index * mod +
                                     fb * (y1 + y2) * 0.5);
        const float got = op.tick((float)freq, (float)ratio, (float)mod,
                                  (float)index, (float)fb, 0, (float)rate);

        worst = std::max(worst, std::fabs(got - want));
        y2 = y1;
        y1 = want;
        phase += (float)(freq * ratio / rate);
        if (phase >= 1)
            phase -= std::floor(phase);
    }
    report("the FM operator, a second of it", worst, 1e-4);

    printf("%d failure%s\n", failed, failed == 1 ? "" : "s");
    return failed ? 1 : 0;
}
