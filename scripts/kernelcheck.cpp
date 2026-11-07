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
#include "../plugins/env/ad.h"
#include "../plugins/env/adsr.h"
#include "../plugins/misc/midi2freq.h"
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
    report("the sine, over a hundred turns either way", worst, 1.5e-6);

    report("the crest and the trough are 1 and -1 exactly",
           std::fabs(thkSin2pi(0.25f) - 1) + std::fabs(thkSin2pi(-0.25f) + 1) +
           std::fabs(thkSin2pi(1.25f) - 1), 0);

    report("just short of a whole turn either way is next to 0",
           std::fabs(thkSin2pi(-1e-9f)) + std::fabs(thkSin2pi(1 - 1e-9f)) +
           std::fabs(thkSin2pi(-3 - 1e-9f)), 1e-6);

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
        const float got = op.tick(thkFmop::ctl((float)freq, (float)ratio, (float)fb,
                                               (float)rate),
                                  (float)mod, (float)index, 0);

        worst = std::max(worst, std::fabs(got - want));
        y2 = y1;
        y1 = want;
        phase += (float)(freq * ratio / rate);
        if (phase >= 1)
            phase -= std::floor(phase);
    }
    report("the FM operator, a second of it", worst, 1e-4);

    /* A note's frequency, against the double the plugin used to keep:
       a rounding or two, and the clamps. */
    worst = 0;
    for (float note = -40; note < 160; note += 0.0137f)
    {
        const double want = std::min(440 * std::pow(2.0, (note - 69) / 12.0),
                                     32000 / 2.0 - 1);

        worst = std::max(worst, std::fabs(thkMidi2freq(note, 32000) - want) / want);
    }
    report("a note's frequency, relative to the double", worst, 2.5e-7);
    worst = std::fabs(thkMidi2freq(NAN, 32000)) +
            std::fabs(thkMidi2freq(-INFINITY, 32000)) +
            std::fabs(thkMidi2freq(INFINITY, 32000) - 15999) +
            std::fabs(thkMidi2freq(4210, 32000) - 15999);
    report("nothing is 0 Hz, and past the top is the top", worst, 0);

    /* The envelopes' shapes: (sample, level, sounding) where it matters. */
    struct Shape
    {
        const char *name;
        int         kind;           /* 0 ad, 1 adsr free, 2 adsr gated */
        int         releaseAt;      /* gated: the trigger falls here */
        struct { int n; float level; bool play; } at[5];
    };
    static const Shape SHAPES[] = {
        { "env::ad: half a sine up, half down, then done", 0, 0,
          { { 0, 0, true }, { 50, 0.4f, true }, { 99, 0.8f, true },
            { 399, 0, true }, { 400, 0, false } } },
        { "env::adsr free-running: up, down to the sustain, held", 1, 0,
          { { 0, 0, true }, { 99, 0.792f, true }, { 400, 0.3f, true },
            { 2000, 0.3f, true }, { 2001, 0.3f, true } } },
        { "env::adsr gated: up, sustain, released to silence", 2, 1000,
          { { 0, 0, true }, { 400, 0.3f, true }, { 1000, 0.3f, true },
            { 1150, 0.052f, true }, { 1300, 0, false } } },
        { "env::adsr gated: released mid-decay, from where it was", 2, 251,
          { { 0, 0, true }, { 250, 0.610f, true }, { 350, 0.232f, true },
            { 451, 0.002f, true }, { 452, 0, false } } },
    };

    for (const Shape &sh : SHAPES)
    {
        thkAd ad;
        thkAdsrFree fr;
        thkAdsrGated gt;
        char why[160] = "";
        int k = 0;

        for (int n = 0; n <= 2001 && k < 5; n++)
        {
            float play = 0, y;

            if (sh.kind == 0)
                y = ad.tick(thkAd::ctl(100, 300, 0.8f), 0, play);
            else if (sh.kind == 1)
                y = fr.tick(thkAdsr(100, 300, 0.3f, 200, 0.8f), 1, 0, play);
            else
                y = gt.tick(thkAdsr(100, 300, 0.3f, 200, 0.8f), n < sh.releaseAt ? 1 : 0, 0, play);
            if (n == sh.at[k].n)
            {
                if (std::fabs(y - sh.at[k].level) > 0.01f || (play > 0) != sh.at[k].play)
                    snprintf(why, sizeof(why), "at %d: %.3f, %s; wanted %.3f, %s", n,
                             y, play > 0 ? "sounding" : "done", sh.at[k].level,
                             sh.at[k].play ? "sounding" : "done");
                k++;
            }
        }
        report(sh.name, why[0] ? 1 : 0, 0);
        if (why[0])
            printf("  %s\n", why);
    }

    printf("%d failure%s\n", failed, failed == 1 ? "" : "s");
    return failed ? 1 : 0;
}
