/*
 * Copyright (C) 2004-2026 Metaphonic Labs
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

/* A state-variable filter in its trapezoidal form, with one convention:
 * cutoff in hertz, resonance from 0 to 1, three outputs off the same two
 * integrators.
 *
 * The tree has fifteen filters and no two of them agree. filt::moog wants a
 * cutoff between 0 and 1 and means a fraction of the rate; filt::res2pole2
 * wants hertz; filt::ds wants a fraction again; filt::res2pole's resonance
 * has a floor of about 0.5 below which it is clamped. Every one of those is
 * defensible on its own and the set is not learnable. This is the one to
 * reach for, and the reference in docs/NODES.md is what makes the difference
 * visible.
 *
 * The arithmetic is the topology-preserving transform (Zavalishin; the
 * direct form here is Andrew Simper's). Two integrators, each trapezoidal:
 *
 *     v3 = in - ic2;   v1 = a1*ic1 + a2*v3;   v2 = ic2 + a2*ic1 + a3*v3
 *     ic1 = 2*v1 - ic1;   ic2 = 2*v2 - ic2
 *
 * with a1 = 1/(1 + g*(g + k)), a2 = g*a1, a3 = g*a2, g = tan(pi*fc/rate)
 * and k = 1/Q. What that buys over the difference equations in the rest of
 * filt/ is that the cutoff is exact at every setting -- the tan() prewarp is
 * what makes it so -- and that the state cannot leave the stable region for
 * any g > 0 and k > 0, which is the property dspsweep exists to check and
 * which filt::res2pole2 and filt::moog only have because they are clamped
 * into it.
 *
 * The three outputs sum to the input, sample for sample:
 *
 *     low + band + high == in
 *
 * because the band output here is k*v1 rather than v1 -- the bandpass
 * normalized to a peak gain of 1 rather than one of Q. That is worth having
 * for its own sake (a resonant band output whose level depends on the
 * resonance is a control that changes two things at once) and it means the
 * three can be mixed back into the input, which is what a filter that offers
 * all three is for.
 *
 * It runs at the sample rate and not at twice it. Oversampling by two would
 * move the transform's squashing an octave up, and the measurement says that
 * is not what it buys: an upsample is only as good as its interpolator, and
 * the cheap one -- the midpoint between this sample and the last -- costs the
 * whole signal a factor of cos(pi*f/2*rate) squared on the way in. That is 3%
 * at five kilohertz and 2.7 dB at fifteen, taken off every output including
 * the high one, which is a bigger error over a bigger span than the warping
 * it would fix. statecheck measured both; the figure above is the one it
 * measured, to three places. An upsampler good enough to be worth it is a
 * polyphase FIR and four times the cost of the filter it feeds.
 *
 * What is left of the warping is what every bilinear filter has: the cutoff
 * is exact at every setting, because the tan() prewarp is exactly the
 * correction for it, and the slope above the cutoff compresses as the cutoff
 * approaches Nyquist. A lowpass at a quarter of the rate is a quarter of the
 * rate; it is the octave above it that is not quite an octave.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

/* The resonance ceiling.
 *
 * `res' is mapped to the damping k = 2 - 2*res, so res of 1 is k of 0: two
 * integrators with nothing damping them, which is an oscillator rather than a
 * filter. It is stable -- the state goes round a circle and stays on it --
 * but a sine at the cutoff sustains for ever and a signal at the cutoff grows
 * without bound, so the top of the range is short of it. 0.99 leaves k at
 * 0.02, which is a Q of 50: as resonant as anything here is asked to be, and
 * still a filter. */
#define RESMAX 0.99f

/* How near Nyquist a cutoff may be asked for.
 *
 * g is tan(pi*fc/rate), which runs to infinity at half the rate: the prewarp
 * is the correction for the transform's squashing, and at Nyquist the
 * squashing is total. 0.49 puts the ceiling a fiftieth of the rate short of
 * it, where g is 32 -- a filter that passes everything, which is what a
 * cutoff at Nyquist means. */
#define CUTOFF_CEIL 0.49f

/* Below this the state is zero. Six hundred decibels under full scale:
   nothing that can be heard, and well clear of the denormals. */
#define SVF_FLUSH 1e-30f

enum {IN_ARG, IN_CUTOFF, IN_RES, OUT_LOW, OUT_BAND, OUT_HIGH, INOUT_LAST};
int args[INOUT_LAST + 1];

static const char desc[] = "State-variable filter: low, band and high";
thPlugin::State    mystate = thPlugin::ACTIVE;

void module_cleanup (thPlugin *plugin)
{
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[IN_ARG] = plugin->regArg("in", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_ARG], "Signal in");
    plugin->setArgRange(args[IN_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[IN_ARG], "full scale");

    args[IN_CUTOFF] = plugin->regArg("cutoff", thPlugin::ARG_IN);
    /* Hertz, like filt::res2pole2 and unlike filt::moog. No numeric range,
       for the reason every hertz arg in the tree gives: the ceiling is
       Nyquist and that is not a number module_init knows. */
    plugin->setArgDesc(args[IN_CUTOFF],
                       "Cutoff in hertz, 0 to 0.49 of the sample rate; "
                       "exact at every setting");
    plugin->setArgUnits(args[IN_CUTOFF], "Hz");

    args[IN_RES] = plugin->regArg("res", thPlugin::ARG_IN);
    /* A real range, unlike every other filter here: the whole span is
       stable and the whole span is usable. See RESMAX for where it stops. */
    plugin->setArgDesc(args[IN_RES],
                       "Resonance: 0 is flat, 0.99 is a Q of 50. Stable "
                       "across the whole range");
    plugin->setArgRange(args[IN_RES], 0, RESMAX);
    plugin->setArgUnits(args[IN_RES], "0..1");

    args[OUT_LOW] = plugin->regArg("out_low", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_LOW], "Low pass, 12 dB an octave");
    plugin->setArgRange(args[OUT_LOW], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[OUT_LOW], "full scale");
    args[OUT_BAND] = plugin->regArg("out_band", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_BAND],
                       "Band pass, peaking at 1 whatever the resonance is");
    plugin->setArgRange(args[OUT_BAND], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[OUT_BAND], "full scale");
    args[OUT_HIGH] = plugin->regArg("out_high", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_HIGH],
                       "High pass; the three outputs sum to the input");
    plugin->setArgRange(args[OUT_HIGH], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[OUT_HIGH], "full scale");

    args[INOUT_LAST] = plugin->regArg("last", thPlugin::ARG_STATE);

    return 0;
}

/* The output stage, and the one place the filter is not exact: a resonant
   peak is up to fifty times what went in, and a node that can hand the mix a
   fifty is a node that empties the master limiter. Clipped rather than
   scaled, which is what filt::res2pole2 does and what the rest of the tree
   expects an audio arg to arrive as. */
static inline float clip (float x)
{
    if (x > TH_MAX)
        return TH_MAX;

    if (x < TH_MIN)
        return TH_MIN;

    return x;
}

/* The trapezoidal coefficients for a cutoff in hertz and a resonance. */
static void coefficients (float cut, float res, unsigned int samples,
                          float *k, float *a1, float *a2, float *a3)
{
    const double g = tan(M_PI * thClampArg(cut, 0.0f, CUTOFF_CEIL * samples) /
                         (double)samples);
    const double kk = 2.0 - 2.0 * thClampArg(res, 0.0f, RESMAX);
    const double d = 1.0 / (1.0 + g * (g + kk));

    *k = (float)kk;
    *a1 = (float)d;
    *a2 = (float)(g * d);
    *a3 = (float)(g * g * d);
}

/* With nothing coming in, rounding keeps a resonant filter's state circling
   just above zero, in the denormals, for as long as the note lasts -- where
   some processors take a hundred times as long over every multiply. */
static inline float flush (float x)
{
    return fabsf(x) < SVF_FLUSH ? 0.0f : x;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out_low, *out_band, *out_high;
    float *out_last;
    thArg *in_arg, *in_cutoff, *in_res;
    thArg *inout_last;
    unsigned int i;
    float ic1, ic2;

    /* The coefficients, and the cutoff and resonance they were worked out
       from. A tan() a sample would be most of the cost of the filter and
       neither number usually moves; a NaN never compares equal to itself, so
       it takes the recompute path where the clamps answer for it. */
    float lastCut = 0, lastRes = 0;
    float a1 = 0, a2 = 0, a3 = 0, k = 2;
    bool have = false;

    out_low = mod->getArg(node, args[OUT_LOW])->allocate(windowlen);
    out_band = mod->getArg(node, args[OUT_BAND])->allocate(windowlen);
    out_high = mod->getArg(node, args[OUT_HIGH])->allocate(windowlen);

    inout_last = mod->getArg(node, args[INOUT_LAST]);
    ic1 = (*inout_last)[0];
    ic2 = (*inout_last)[1];
    out_last = inout_last->allocate(2);

    /* Feedback state: one non-finite sample would otherwise be read back for
       the life of the note. The filter cannot produce one on its own -- that
       is the point of the form -- so this is about what arrives. */
    if (!thIsFinite(ic1) || !thIsFinite(ic2))
        ic1 = ic2 = 0;

    in_arg = mod->getArg(node, args[IN_ARG]);
    in_cutoff = mod->getArg(node, args[IN_CUTOFF]);
    in_res = mod->getArg(node, args[IN_RES]);

    float in[windowlen];

    in_arg->getBuffer(in, windowlen);

    /* A cutoff and a resonance that are one value each are one set of
       coefficients, and the loop is the recursion and nothing else: the
       outputs are clipped afterwards, a pass the compiler can widen. */
    if (in_cutoff->len() <= 1 && in_res->len() <= 1)
    {
        bool quiet = ic1 == 0 && ic2 == 0;

        for (i = 0; quiet && i < windowlen; i++)
            quiet = in[i] == 0;

        /* A filter at rest with nothing coming in stays at rest. */
        if (quiet)
        {
            memset(out_low, 0, windowlen * sizeof(float));
            memset(out_band, 0, windowlen * sizeof(float));
            memset(out_high, 0, windowlen * sizeof(float));
        }
        else
        {
            coefficients((*in_cutoff)[0], (*in_res)[0], samples, &k, &a1,
                         &a2, &a3);

            for (i = 0; i < windowlen; i++)
            {
                const float v0 = thClampArg(in[i], TH_MIN, TH_MAX);
                const float v3 = v0 - ic2;
                const float v1 = a1 * ic1 + a2 * v3;
                const float v2 = ic2 + a2 * ic1 + a3 * v3;

                ic1 = flush(2.0f * v1 - ic1);
                ic2 = flush(2.0f * v2 - ic2);

                in[i] = v0;
                out_band[i] = v1;
                out_low[i] = v2;
            }

            for (i = 0; i < windowlen; i++)
            {
                const float v1 = out_band[i], v2 = out_low[i];

                out_low[i] = clip(v2);
                out_band[i] = clip(k * v1);
                out_high[i] = clip(in[i] - k * v1 - v2);
            }
        }
    }
    else
        for (i = 0; i < windowlen; i++)
        {
            const float v0 = thClampArg(in[i], TH_MIN, TH_MAX);
            const float cut = (*in_cutoff)[i];
            const float res = (*in_res)[i];
            float v1, v2, v3;

            if (!have || cut != lastCut || res != lastRes)
            {
                coefficients(cut, res, samples, &k, &a1, &a2, &a3);
                lastCut = cut;
                lastRes = res;
                have = true;
            }

            v3 = v0 - ic2;
            v1 = a1 * ic1 + a2 * v3;
            v2 = ic2 + a2 * ic1 + a3 * v3;

            ic1 = flush(2.0f * v1 - ic1);
            ic2 = flush(2.0f * v2 - ic2);

            out_low[i] = clip(v2);
            out_band[i] = clip(k * v1);
            out_high[i] = clip(v0 - k * v1 - v2);
        }

    out_last[0] = ic1;
    out_last[1] = ic2;

    return 0;
}
