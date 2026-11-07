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
#include "../plugins/filt/svf.h"
#include "../plugins/mixer/pan.h"
#include "../plugins/osc/blep.h"
#include "../plugins/osc/noise.h"
#include "../plugins/osc/simple.h"

static int failed = 0;

/* By the bits: fast math may take a NaN to equal anything. */
template <class T>
static bool
same (const T &a, const T &b)
{
    return memcmp(&a, &b, sizeof(T)) == 0;
}

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

    /* And 0, which a NaN is held to, from a volatile too: worked out where
       the NaN's is, not folded where fast math may round it otherwise. */
    volatile float vzero = 0;
    const float zero = vzero;

    report("a sine of NaN is 0, not a read past the table", thkSin2pi(nan) == 0);
    /* What it gives back is fast math's to say; past the table is a crash. */
    (void)thkLn1e(nan);
    report("the envelopes' log of NaN reads no table", true);
    report("a NaN frequency is the slowest", thkBoundFreq(nan, 32000) == 32000 / 16777216.0f);

    report("a NaN pan is the middle", same(thkPan::ctl(nan), thkPan::ctl(zero)));
    {
        /* A NaN width is the narrowest, or, where fast math takes it to equal
           0, the middle: either way neither part of the cycle is empty. */
        const thkSimple::Ctl c = thkSimple::ctl(nan, 1, 1, nan, nan, 32000);

        report("osc::simple's NaN frequency, width and waveform are the slowest, a "
               "width and the sine",
               c.wavelength == thkSimple::ctl(zero, 1, 1, 0, 0, 32000).wavelength &&
               c.halfwave > 0 && c.halfwave < c.wavelength && c.waveform == 0);
    }
    report("osc::blep's NaN frequency, waveform and width are the slowest, the saw "
           "and the middle",
           same(thkBlep::ctl(nan, nan, nan, 32000), thkBlep::ctl(zero, zero, 0.5f, 32000)));
    {
        const thkBlep::Ctl c = thkBlep::ctl(1000, 0, 0.5f, 32000);
        thkBlep a, b;
        float sync, edge;
        bool ok = true;

        for (int i = 0; i < 100; i++)
            ok = ok && same(a.tick(c, nan, nan, sync, edge), b.tick(c, zero, zero, sync, edge));
        report("osc::blep takes a NaN start and reset as none", ok);
    }
    report("a NaN color and level are white at full scale",
           same(thkNoise::ctl(nan, nan), thkNoise::ctl(zero, 1)));
    report("a NaN cutoff and resonance are 0",
           same(thkSvf::ctl(nan, nan, 32000), thkSvf::ctl(zero, zero, 32000)));
    {
        const thkSvf::Ctl c = thkSvf::ctl(1000, 0.5f, 32000);
        thkSvf a, b;
        float band, high;
        bool ok = true;

        for (int i = 0; i < 100; i++)
            ok = ok && same(a.tick(c, nan, band, high), b.tick(c, zero - 1, band, high));
        report("filt::svf takes a NaN in as the floor", ok);
    }

    printf("%d failure%s\n", failed, failed == 1 ? "" : "s");
    return failed ? 1 : 0;
}
