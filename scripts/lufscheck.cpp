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
 * lufscheck -- scripts/Loudness.h against what BS.1770 says a known signal
 * measures: a 1 kHz sine at a peak of -23 dBFS in both channels reads -23
 * LUFS (EBU Tech 3341's first case); silence before it changes
 * nothing; a passage 20 LU under the rest is gated out, and one 5 LU under
 * is not.
 */

#include <math.h>
#include <stdio.h>

#include <string>
#include <vector>

#include "Loudness.h"

static int failures = 0;

static void check (bool ok, const std::string &what, double got)
{
    printf("%s  %s: %.2f LUFS\n", ok ? "ok  " : "FAIL", what.c_str(), got);
    failures += !ok;
}

/* `seconds' of a 1 kHz sine at `dbfs' (peak) in both channels, or of
   silence for a level of -HUGE_VAL. */
static void tone (Loudness &m, double rate, double seconds, double dbfs)
{
    const double a = isfinite(dbfs) ? pow(10.0, dbfs / 20) : 0;

    for (long i = 0; i < (long)(seconds * rate); i++)
    {
        const float v = (float)(a * sin(2 * M_PI * 1000 * i / rate));
        const float frame[2] = { v, v };

        m.add(frame);
    }
}

int main (void)
{
    const double rates[] = { 44100, 48000 };

    for (double rate : rates)
    {
        const std::string at = " at " + std::to_string((int)rate) + " Hz";

        Loudness steady(2, rate);
        tone(steady, rate, 20, -23);
        const double l = steady.integrated();
        check(fabs(l + 23) < 0.1, "-23 dBFS at 1 kHz reads -23" + at, l);

        Loudness quiet(2, rate);
        tone(quiet, rate, 10, -HUGE_VAL);
        tone(quiet, rate, 20, -23);
        const double q = quiet.integrated();
        check(fabs(q + 23) < 0.1, "silence before it reads the same" + at, q);

        Loudness under(2, rate);
        tone(under, rate, 20, -23);
        tone(under, rate, 20, -43);
        const double u = under.integrated();
        check(fabs(u + 23) < 0.1, "a passage 20 LU under is gated out" + at,
              u);

        Loudness near(2, rate);
        tone(near, rate, 20, -23);
        tone(near, rate, 20, -28);
        const double n = near.integrated();
        const double mean = Loudness::lufs((pow(10.0, -2.3) +
                                            pow(10.0, -2.8)) / 2) -
                            Loudness::lufs(pow(10.0, -2.3)) - 23;
        check(fabs(n - mean) < 0.1, "a passage 5 LU under counts" + at, n);
    }

    return failures ? 1 : 0;
}
