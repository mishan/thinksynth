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
#include "../plugins/delay/allpass.h"
#include "../plugins/delay/echo.h"
#include "../plugins/env/ad.h"
#include "../plugins/env/adsr.h"
#include "../plugins/math/sin.h"
#include "../plugins/misc/latch.h"
#include "../plugins/misc/midi2freq.h"
#include "../plugins/mixer/pan.h"
#include "../plugins/dice.h"
#include "../plugins/dist/saturate.h"
#include "../plugins/filt/svf.h"
#include "../plugins/osc/blep.h"
#include "../plugins/osc/noise.h"
#include "../plugins/osc/fmop.h"
#include "../plugins/osc/simple.h"

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

/* The polyBLEP residual for a step at 0 of a cycle stepping dt a sample. */
static double
refBlep (double t, double dt)
{
    if (t < dt)
        return 2 * (t / dt) - (t / dt) * (t / dt) - 1;
    if (t > 1 - dt)
        return ((t - 1) / dt) * ((t - 1) / dt) + 2 * ((t - 1) / dt) + 1;
    return 0;
}

/* Where a step of h falls d of a sample before the next: its residual on
   this sample and on the next. */
static double refStepBefore (double h, double d) { return h / 2 * d * d; }
static double refStepAfter (double h, double d) { return h / 2 * (2 * d - d * d - 1); }

/* osc::simple's shapes, from where in the cycle and the level. */
static double
refSimple (int waveform, double r, double hi)
{
    const double a = 4 * r - 1, b = 4 * r - 3;

    switch (waveform)
    {
    case 1: return hi * (2 * r - 1);
    case 2: return r < 0.5 ? -hi : hi;
    case 3: return r < 0.5 ? hi * a : -hi * b;
    case 4: return r < 0.5 ? hi * std::sqrt(1 - a * a) : -hi * std::sqrt(1 - b * b);
    case 5: return r < 0.5 ? hi * (1 - a * a) : hi * (b * b - 1);
    default: return hi * std::sin(2 * M_PI * r);
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

    /* The envelopes' log, over the 1 to e their curves take it over, and
       beyond it as logf. */
    worst = 0;
    for (double x = 1; x <= 2.718281828; x += 1e-5)
        worst = std::max(worst, std::fabs(thkLn1e((float)x) - std::log((double)(float)x)));
    report("the envelopes' log, from 1 to e", worst, 5e-7);
    report("the envelopes' log, 1 to 0 and e to 1",
           std::fabs(thkLn1e(1)) + std::fabs(thkLn1e(2.71828182845904523536f) - 1), 1.2e-7);
    report("the envelopes' log, either side of 1 to e, as logf",
           std::fabs(thkLn1e(0.5f) - logf(0.5f)) + std::fabs(thkLn1e(3) - logf(3)), 0);

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
    report("the FM operator, a second of it", worst, 1e-5);

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

    /* math::sin against the double sine of the same float turns, out to a
       video patch's thousands of them; a 0 wavelength or amp is 1. */
    worst = 0;
    for (const float wavelength : { 1.0f, 0.05f, 0.0f })
        for (const float amp : { 1.0f, 0.4f, 0.0f })
            for (float index = -3000; index < 3000; index += 0.0731f)
            {
                const float turns = index / (wavelength == 0 ? 1 : wavelength);
                const double want = std::sin(turns * 2 * M_PI) * (amp == 0 ? 1 : amp);

                worst = std::max(worst, std::fabs(thkSin(index, wavelength, amp) - want));
            }
    report("math::sin, against the double", worst, 1.3e-6);

    /* The pan's sides against cos and sin of (pan + 1) pi / 4, and their
       powers summing to 1 wherever it's put; past the ends is the end. */
    worst = 0;
    for (float pan = -1.2f; pan <= 1.2f; pan += 0.001f)
    {
        const double t = (std::max(-1.0f, std::min(1.0f, pan)) + 1) * (M_PI / 4);
        const thkPan::Ctl c = thkPan::ctl(pan);

        worst = std::max(worst, std::max(std::fabs(c.left - std::cos(t)),
                                         std::fabs(c.right - std::sin(t))));
    }
    report("the pan's two sides, from end to end", worst, 2e-7);
    worst = 0;
    for (float pan = -1; pan <= 1; pan += 0.001f)
    {
        const thkPan::Ctl c = thkPan::ctl(pan);

        worst = std::max(worst, (double)std::fabs(c.left * c.left + c.right * c.right - 1));
    }
    report("the pan's sides' powers sum to the input's", worst, 3e-7);
    report("a pan that isn't a number is the middle",
           std::fabs(thkPan::ctl(NAN).left - (float)M_SQRT1_2) +
           std::fabs(thkPan::ctl(NAN).right - (float)M_SQRT1_2), 1.5e-6);

    /* osc::simple at 100 samples a cycle: each waveform where its shape
       says -- the square low for the first half, as its sign trick has it
       -- and a cycle begun every 100 samples, the first on the 100th. */
    {
        struct Point { int waveform, at; float want; };
        static const Point POINTS[] = {
            { 0, 25, 1 }, { 0, 75, -1 }, { 1, 1, -1 }, { 1, 50, 0 }, { 2, 10, -0.8f },
            { 2, 60, 0.8f }, { 3, 25, 0 }, { 3, 50, 1 }, { 5, 25, 1 }, { 5, 75, -1 },
        };
        char why[120] = "";

        for (const Point &p : POINTS)
        {
            thkSimple osc;
            const thkSimple::Ctl c = thkSimple::ctl(441, 1, p.waveform == 2 ? 0.8f : 1, 0,
                                                    (float)p.waveform, 44100);
            float sync, y = 0;

            /* The phase steps before it's read: sample n is n + 1 in. */
            for (int n = 0; n < p.at; n++)
                y = osc.tick(c, 0, 0, 0, sync);
            if (std::fabs(y - p.want) > 0.03f)
                snprintf(why, sizeof(why), "waveform %d at %d: %.3f, wanted %.3f",
                         p.waveform, p.at, y, p.want);
        }
        report("osc::simple's waveforms, where their shapes say", why[0] ? 1 : 0, 0);
        if (why[0])
            printf("  %s\n", why);

        thkSimple osc;
        const thkSimple::Ctl c = thkSimple::ctl(441, 1, 1, 0, 1, 44100);
        int last = -1, worstGap = 0, n = 0;

        for (int i = 0; i < 1000; i++)
        {
            float sync;

            osc.tick(c, 0, 0, 0, sync);
            if (sync > 0)
            {
                if (last >= 0)
                    worstGap = std::max(worstGap, std::abs(i - last - 100));
                last = i;
                n++;
            }
        }
        report("osc::simple begins a cycle every 100 samples", n == 9 ? worstGap : 99, 0);
    }

    /* osc::simple's every waveform, every sample of three cycles of 100,
       at a level of 0.8, against its shape in double; and the saw with
       its first part 30 of them. */
    {
        double worstShape = 0;

        for (int waveform = 0; waveform <= 6; waveform++)
        {
            const float pw = waveform == 6 ? 0.3f : 0;
            const int shape = waveform == 6 ? 1 : waveform;
            const thkSimple::Ctl c = thkSimple::ctl(441, 1, 0.8f, pw, (float)shape, 44100);
            thkSimple osc;

            for (int n = 0; n < 300; n++)
            {
                float sync;
                const double pos = n % 100 + 1, first = pw ? 100 * (double)pw : 50;
                const double r = pos < first ? pos / (2 * first)
                                             : (pos - first) * 0.5 / (100 - first) + 0.5;

                worstShape = std::max(worstShape, std::fabs(osc.tick(c, 0, 0, 0, sync) -
                                                            refSimple(shape, r, 0.8)));
            }
        }
        report("osc::simple's every waveform, every sample", worstShape, 1e-5);
    }

    /* osc::blep at 100 samples a cycle: the saw a line away from its edge
       and the pulse its levels, a cycle begun every 100 samples, the first
       on the 100th, and the edge saying how far into the sample the wrap
       fell. */
    {
        struct Point { int wave, at; float want; };
        static const Point POINTS[] = {
            { 0, 25, -0.5f }, { 0, 50, 0 }, { 0, 75, 0.5f }, { 1, 25, 1 }, { 1, 75, -1 },
        };
        char why[120] = "";

        for (const Point &p : POINTS)
        {
            thkBlep osc;
            const thkBlep::Ctl c = thkBlep::ctl(441, (float)p.wave, 0.5f, 44100);
            float sync, edge, y = 0;

            /* Sample n reads the phase before it steps: n / 100. */
            for (int n = 0; n <= p.at; n++)
                y = osc.tick(c, 0, 0, sync, edge);
            if (std::fabs(y - p.want) > 1e-4f)
                snprintf(why, sizeof(why), "wave %d at %d: %.4f, wanted %.4f", p.wave,
                         p.at, y, p.want);
        }
        report("osc::blep's saw and pulse away from their edges", why[0] ? 1 : 0, 0);
        if (why[0])
            printf("  %s\n", why);

        thkBlep osc;
        const thkBlep::Ctl c = thkBlep::ctl(441, 0, 0.5f, 44100);
        int last = -1, gaps = 0, n = 0;
        float worstEdge = 0;

        for (int i = 0; i < 1000; i++)
        {
            float sync, edge;

            osc.tick(c, 0, 0, sync, edge);
            if (sync > 0)
            {
                gaps = std::max(gaps, last >= 0 ? std::abs(i - last - 100) : 0);
                last = i;
                n++;
                worstEdge = std::max(worstEdge, edge >= 0 && edge <= 1 ? 0.0f : 1.0f);
            }
        }
        report("osc::blep begins a cycle every 100 samples, its edge in the sample",
               n == 9 ? gaps + worstEdge : 99, 0);
    }

    /* osc::blep's saw, pulse and triangle against the polyBLEP formula in
       double, its phase stepped in float as the kernel's is: 440 Hz, so
       the wraps fall anywhere in a sample, over a hundred cycles, the
       triangle the pulse integrated with its leak. */
    {
        const float dt = 440.0f / 44100, pw = 0.3f;
        double worstWave[3] = { 0, 0, 0 };

        for (int wave = 0; wave < 3; wave++)
        {
            const thkBlep::Ctl c = thkBlep::ctl(440, (float)wave, pw, 44100);
            thkBlep osc;
            float phase = 0;
            double tri = -(double)pw * (1 - pw);

            for (int n = 0; n < 10000; n++)
            {
                float sync, edge;
                const double t2 = std::fmod((double)phase + 1 - pw, 1.0);
                const double pulse = (phase < pw ? 1 : -1) + refBlep(phase, dt) -
                                     refBlep(t2, dt);
                double want = 2 * (double)phase - 1 - refBlep(phase, dt);

                if (wave == 1)
                    want = pulse;
                else if (wave == 2)
                {
                    tri = dt * (pulse - (2 * (double)pw - 1)) + (1 - dt / 64.0) * tri;
                    want = tri / (pw * (1 - (double)pw));
                }
                worstWave[wave] = std::max(worstWave[wave],
                                           std::fabs(osc.tick(c, 0, 0, sync, edge) - want));
                phase += dt;
                if (phase >= 1)
                    phase -= std::floor(phase);
            }
        }
        report("osc::blep's saw across its wraps", worstWave[0], 1e-4);
        report("osc::blep's pulse across both its edges", worstWave[1], 1e-4);
        report("osc::blep's triangle, leak and all", worstWave[2], 1e-4);

        /* A reset a quarter into sample 37, at about 0.37 of a cycle: the
           step down to the start corrected either side of where it fell,
           and the cycle begun again three quarters of a step before the
           next sample. The pulse is low there, so its step is up. */
        double worstReset = 0;

        for (int wave = 0; wave < 2; wave++)
        {
            const thkBlep::Ctl c = thkBlep::ctl(441, (float)wave, pw, 44100);
            const double step = (double)c.dt, d = 0.75;
            thkBlep osc;
            float sync, edge, phase = 0;

            for (int n = 0; n < 37; n++)
            {
                osc.tick(c, 0, 0, sync, edge);
                phase += c.dt;
            }

            const double at = phase + 0.25 * step;
            const double h = wave ? 2 : -2 * at;
            const double want[3] = {
                (wave ? -1 : 2 * (double)phase - 1) + refStepBefore(h, d),
                (wave ? 1 : 2 * d * step - 1) + refStepAfter(h, d),
                wave ? 1 : 2 * (d + 1) * step - 1,
            };

            for (int k = 0; k < 3; k++)
                worstReset = std::max(worstReset, std::fabs(osc.tick(c, 0, k ? 0 : 0.25f,
                                                                     sync, edge) - want[k]));
        }
        report("osc::blep's saw and pulse reset partway into a sample", worstReset, 1e-5);
    }

    /* The noise's white is dice.h's stream through the double it went
       through, bit for bit. */
    {
        thkNoise noise;
        float dice[2];
        int differ = 0;

        noise.seed(12345);
        thDiceSeedBits(dice, 12345);
        for (int i = 0; i < 100000; i++)
        {
            const float want = (float)(2 * thDiceNext(dice) - 1);
            const float got = noise.tick(thkNoise::ctl(0, 1));

            differ += memcmp(&want, &got, sizeof(float)) != 0;
        }
        report("the noise is dice.h's stream, to the bit", differ, 0);
    }

    /* Pink and brown from that stream through their filters in double,
       from the published coefficients -- Kellett's for pink, a pole at
       0.995 for brown -- each scaled to put five of its standard
       deviations, worked out from them, at the peak: a second's pass of
       twenty, every sample. Pink's sigma in noise.h is a measurement, a
       quarter of a thousandth over its formula. */
    {
        static const double P[3] = { 0.99765, 0.96300, 0.57000 };
        static const double G[3] = { 0.0990460, 0.2965164, 1.0526913 };
        const double gw = 0.1848, pb = 0.995;
        double var = gw * gw;

        for (int i = 0; i < 3; i++)
        {
            var += 2 * gw * G[i];
            for (int j = 0; j < 3; j++)
                var += G[i] * G[j] / (1 - P[i] * P[j]);
        }

        const double sigma[3] = { 0, std::sqrt(var / 3), std::sqrt((1 - pb) / (1 + pb) / 3) };

        for (int color = 1; color <= 2; color++)
        {
            thkNoise noise;
            float dice[2];
            double b[3] = { 0, 0, 0 }, brown = 0, worstAbs = 0, got2 = 0, want2 = 0;

            noise.seed(777);
            thDiceSeedBits(dice, 777);
            for (int i = 0; i < 20 * 44100; i++)
            {
                const double w = 2 * thDiceNext(dice) - 1;

                for (int j = 0; j < 3; j++)
                    b[j] = P[j] * b[j] + G[j] * w;
                brown = pb * brown + (1 - pb) * w;

                double want = (color == 1 ? b[0] + b[1] + b[2] + gw * w : brown) /
                              (5 * sigma[color]);

                want = std::max(-1.0, std::min(1.0, want));

                const float got = noise.tick(thkNoise::ctl((float)color, 1));

                worstAbs = std::max(worstAbs, std::fabs(got - want));
                got2 += (double)got * got;
                want2 += want * want;
            }
            report(color == 1 ? "pink noise, its filter, every sample"
                              : "brown noise, its filter, every sample", worstAbs, 4e-4);
            report(color == 1 ? "pink noise, its level" : "brown noise, its level",
                   std::fabs(std::sqrt(got2 / want2) - 1), 4e-4);
        }
    }

    /* The filter's low passes a held level and its high takes it out;
       resonance near the top rings but stays put. */
    {
        thkSvf f;
        const thkSvf::Ctl c = thkSvf::ctl(1000, 0.9f, 44100);
        float low = 0, band, high = 1;

        for (int i = 0; i < 44100; i++)
            low = f.tick(c, 0.5f, band, high);
        report("filt::svf's low passes a held level, its high none of it",
               std::fabs(low - 0.5f) + std::fabs(high), 1e-5);
    }

    /* The filter's three outputs at a quarter, at and four times its
       cutoff against the bilinear transform of 1 / (s^2 + k s + 1) and
       its band and high, k = 2 - 2 res: at the cutoff the low and high
       are Q, 1 / k, the band 1. 1200 Hz at 48k, so every tone is a whole
       number of samples a cycle, a tenth of full scale in, a second to
       settle and a second measured. */
    {
        static const float RES[] = { 0, 0.5f, 0.9f };
        static const double FREQ[] = { 300, 1200, 4800 };
        const double rate = 48000, fc = 1200;
        double worstRel = 0;

        for (float res : RES)
            for (double f : FREQ)
            {
                const thkSvf::Ctl c = thkSvf::ctl((float)fc, res, (float)rate);
                const double k = 2 - 2 * (double)res;
                const double W = std::tan(M_PI * f / rate) / std::tan(M_PI * fc / rate);
                const double D = std::hypot(1 - W * W, k * W);
                const double want[3] = { 1 / D, k * W / D, W * W / D };
                double si[3] = { 0, 0, 0 }, co[3] = { 0, 0, 0 };
                thkSvf svf;

                for (int n = 0; n < 2 * 48000; n++)
                {
                    const double ph = 2 * M_PI * f * n / rate;
                    float y[3];

                    y[0] = svf.tick(c, (float)(0.1 * std::sin(ph)), y[1], y[2]);
                    if (n >= 48000)
                        for (int o = 0; o < 3; o++)
                        {
                            si[o] += y[o] * std::sin(ph);
                            co[o] += y[o] * std::cos(ph);
                        }
                }
                for (int o = 0; o < 3; o++)
                    worstRel = std::max(worstRel,
                                        std::fabs(std::hypot(si[o], co[o]) * 2 / 48000 / 0.1 /
                                                  want[o] - 1));
            }
        report("filt::svf's low, band and high, Q and all, around its cutoff", worstRel, 1e-4);
    }

    /* tanh in float, against the double's. */
    worst = 0;
    for (float x = -12; x <= 12; x += 0.0007f)
        worst = std::max(worst, std::fabs(thkTanh(x) - std::tanh((double)x)));
    report("dist::saturate's tanh", worst, 2.5e-7);

    /* Down where 1 - 2 / (e^2x + 1) would cancel to nothing: to its
       size. */
    worst = 0;
    for (double e = -12; e <= -1; e += 0.01)
        for (int sign = -1; sign <= 1; sign += 2)
        {
            const float x = (float)(sign * std::pow(10.0, e));

            worst = std::max(worst, std::fabs(thkTanh(x) / std::tanh((double)x) - 1));
        }
    report("dist::saturate's tanh of a small input, relative to it", worst, 3e-7);

    static const float FACTOR[] = { 0.25f, 1, 3, 8 };

    worst = 0;
    for (float factor : FACTOR)
        for (float in = -1; in <= 1; in += 0.001f)
            worst = std::max(worst, std::fabs(thkSaturate(in, factor) -
                                              std::tanh((double)factor * in)));
    report("dist::saturate by a factor", worst, 2.5e-7);

    /* The envelopes' shapes: (sample, level, sounding) where it matters. */
    struct Shape
    {
        const char *name;
        int         kind;           /* 0 ad, 1 adsr free, 2 adsr gated */
        int         releaseAt;      /* the trigger falls here, or 0 */
        int         resetAt;        /* free: reset rises here, or 0 */
        struct { int n; float level; bool play; } at[5];
    };
    static const Shape SHAPES[] = {
        { "env::ad: half a sine up, half down, then done", 0, 0, 0,
          { { 0, 0, true }, { 50, 0.4f, true }, { 99, 0.8f, true },
            { 399, 0, true }, { 400, 0, false } } },
        { "env::adsr free-running: up, down to the sustain, held", 1, 0, 0,
          { { 0, 0, true }, { 99, 0.792f, true }, { 400, 0.3f, true },
            { 2000, 0.3f, true }, { 2001, 0.3f, true } } },
        { "env::adsr gated: up, sustain, released to silence", 2, 1000, 0,
          { { 0, 0, true }, { 400, 0.3f, true }, { 1000, 0.3f, true },
            { 1150, 0.052f, true }, { 1300, 0, false } } },
        { "env::adsr gated: released mid-decay, from where it was", 2, 251, 0,
          { { 0, 0, true }, { 250, 0.610f, true }, { 350, 0.232f, true },
            { 451, 0.002f, true }, { 452, 0, false } } },
        /* As the gated one: its trigger is a value, not the note's. */
        { "env::adsr free-running: released by its trigger, to silence", 1, 1000, 0,
          { { 0, 0, true }, { 400, 0.3f, true }, { 1000, 0.3f, true },
            { 1150, 0.052f, true }, { 1300, 0, false } } },
        { "env::adsr free-running: a reset starts it again from silence", 1, 0, 1200,
          { { 0, 0, true }, { 400, 0.3f, true }, { 1199, 0.3f, true },
            { 1200, 0, true }, { 1250, 0.4f, true } } },
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
                y = fr.tick(thkAdsr(100, 300, 0.3f, 200, 0.8f),
                            !sh.releaseAt || n < sh.releaseAt ? 1 : 0,
                            n == sh.resetAt && n ? 1 : 0, play);
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

    /* An impulse through the allpass: -g at once, then (1 - g^2) g^(k-1)
       at each k delays; a gain past the bound is the bound's. */
    {
        static const float GAIN[] = { 0, 0.5f, -0.7f, 0.999f, 2 };
        const unsigned int D = 7;

        worst = 0;
        for (float gain : GAIN)
        {
            const double g = std::min(std::max((double)gain, -0.999), 0.999);
            float line[D] = {};
            thkAllpass ap;

            for (unsigned int n = 0; n < 40 * D; n++)
            {
                const double want = n == 0 ? -g : n % D ? 0
                                  : (1 - g * g) * std::pow(g, n / D - 1);

                worst = std::max(worst, std::fabs(ap.tick(line, D, n == 0, gain) - want));
            }
        }
        report("delay::allpass's impulse response", worst, 1e-6);
    }

    /* The echo's ring, its repeats filtered and driven, against the same
       loop in double: an impulse, then noise. */
    {
        struct Loop { float feedback, dry, tone, low, drive, boost; };
        static const Loop LOOPS[] = {
            { 0.6f, 0, 0, 0, 0, 0 }, { 0.6f, 0.3f, 0, 0, 0, 0 },
            { 0.8f, 0, 3000, 200, 0, 0 }, { 0.9f, 0.5f, 5000, 0, 2, 0.4f },
        };
        const unsigned int S = 37;
        const double rate = 44100, pi = 3.14159265358979323846;
        uint32_t seed = 1;

        worst = 0;
        for (const Loop &l : LOOPS)
        {
            float ring[S] = {};
            double ref[S] = {}, lp = 0, hpIn = 0, hpOut = 0;
            thkEcho e;

            for (unsigned int n = 0; n < 50 * S; n++)
            {
                seed = seed * 1664525u + 1013904223u;

                const float in = n == 0 ? 1 : n < 10 * S ? 0 : (float)(seed >> 8) / 16777216 - 0.5f;
                const unsigned int at = n % S;
                const double delay = ref[at];
                double back = delay;

                if (l.tone > 0)
                    back = lp += (1 - std::exp(-2 * pi * l.tone / rate)) * (back - lp);
                if (l.low > 0)
                {
                    hpOut = std::exp(-2 * pi * l.low / rate) * (hpOut + back - hpIn);
                    hpIn = back;
                    back = hpOut;
                }
                if (l.boost > 0)
                    back *= 1 + l.boost;

                const double bend = l.boost > 0 ? std::max((double)l.drive, 1.0) : l.drive;

                if (bend > 0)
                    back = std::tanh(bend * back) / bend;
                ref[at] = l.feedback * back + (1 - l.feedback) * in;

                const double want = (1 - l.dry) * delay + l.dry * in;
                const float got = e.tick(ring, S, in, (float)S, l.feedback, l.dry, l.tone,
                                         l.low, l.drive, l.boost, (float)rate);

                worst = std::max(worst, std::fabs(got - want));
            }
        }
        report("delay::echo's ring, filtered and driven", worst, 1e-5);
    }

    /* Where the echo reads: a whole delay whole rings longer or shorter
       reads where it does, and a NaN as none; no ring passes the input
       through. And how long a ring it asks for. */
    {
        const unsigned int S = 37;
        static const float BEHIND[][2] = {
            { 5, 5 + 1000 * 37 }, { -3, -3 - 1000 * 37 }, { 0, NAN },
        };
        int wrong = 0;

        for (const auto &b : BEHIND)
        {
            float ra[S] = {}, rb[S] = {};
            thkEcho ea, eb;
            uint32_t seed = 7;

            for (unsigned int n = 0; n < 10 * S; n++)
            {
                seed = seed * 1664525u + 1013904223u;

                const float in = (float)(seed >> 8) / 16777216 - 0.5f;

                wrong += ea.tick(ra, S, in, b[0], 0.5f, 0, 0, 0, 0, 0, 44100) !=
                         eb.tick(rb, S, in, b[1], 0.5f, 0, 0, 0, 0, 0, 44100);
            }
        }

        thkEcho none;

        wrong += none.tick(NULL, 0, 0.25f, 3, 0.5f, 0, 0, 0, 0, 0, 44100) !=
                 0.25f;
        wrong += thkEchoRing(0, 100) != 0 || thkEchoRing(-5, 100) != 0 ||
                 thkEchoRing(NAN, 100) != 0 || thkEchoRing(10.7f, 100) != 10 ||
                 thkEchoRing(1e12f, 100) != 6000;
        report("delay::echo reads whole rings on alike, a NaN as none",
               wrong, 0);
    }

    /* A line shorter than where the allpass was is read from its start. */
    {
        float line[3] = { 1, 2, 3 };
        thkAllpass ap;

        ap.pos = 5;

        const float got = ap.tick(line, 3, 0, 0);

        report("delay::allpass starts a shortened line from its start",
               got != 1 || ap.pos != 1, 0);
    }

    /* misc::latch: what came in while the latch was up, held after. */
    {
        static const float WANT[] = { 0, 0, 0, 3, 4, 4, 4, 4, 4, 4, 10, 10 };
        thkLatch l;
        int wrong = 0;

        for (int n = 0; n < 12; n++)
            wrong += l.tick((float)n, n == 3 || n == 4 || n == 10 ? 1 : 0) != WANT[n];
        report("misc::latch takes while its latch is up, and holds", wrong, 0);
    }

    printf("%d failure%s\n", failed, failed == 1 ? "" : "s");
    return failed ? 1 : 0;
}
