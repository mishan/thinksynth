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

#include "svf.h"

/* The resonance ceiling, thkSvf::RESMAX.
 *
 * `res' is mapped to the damping k = 2 - 2*res, so res of 1 is k of 0: two
 * integrators with nothing damping them, which is an oscillator rather than a
 * filter. It is stable -- the state goes round a circle and stays on it --
 * but a sine at the cutoff sustains for ever and a signal at the cutoff grows
 * without bound, so the top of the range is short of it. 0.99 leaves k at
 * 0.02, which is a Q of 50: as resonant as anything here is asked to be, and
 * still a filter. */

/* How near Nyquist a cutoff may be asked for, thkSvf::CUTOFF_CEIL.
 *
 * g is tan(pi*fc/rate), which runs to infinity at half the rate: the prewarp
 * is the correction for the transform's squashing, and at Nyquist the
 * squashing is total. 0.49 puts the ceiling a fiftieth of the rate short of
 * it, where g is 32 -- a filter that passes everything, which is what a
 * cutoff at Nyquist means. */

enum {IN_ARG, IN_CUTOFF, IN_RES, OUT_LOW, OUT_BAND, OUT_HIGH, INOUT_LAST};
std::atomic<int> args[INOUT_LAST + 1];

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
    plugin->setArgSmooth(args[IN_CUTOFF], TH_SMOOTH_MS);
    /* Hertz, like filt::res2pole2 and unlike filt::moog. No numeric range,
       for the reason every hertz arg in the tree gives: the ceiling is
       Nyquist and that is not a number module_init knows. */
    plugin->setArgDesc(args[IN_CUTOFF],
                       "Cutoff in hertz, 0 to 0.49 of the sample rate; "
                       "exact at every setting");
    plugin->setArgUnits(args[IN_CUTOFF], "Hz");

    args[IN_RES] = plugin->regArg("res", thPlugin::ARG_IN);
    plugin->setArgSmooth(args[IN_RES], TH_SMOOTH_MS);
    /* A real range, unlike every other filter here: the whole span is
       stable and the whole span is usable. See thkSvf::RESMAX for where
       it stops. */
    plugin->setArgDesc(args[IN_RES],
                       "Resonance: 0 is flat, 0.99 is a Q of 50. Stable "
                       "across the whole range");
    plugin->setArgRange(args[IN_RES], 0, thkSvf::RESMAX);
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




int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out_low = mod->getArg(node, args[OUT_LOW])->allocate(windowlen);
    float *out_band = mod->getArg(node, args[OUT_BAND])->allocate(windowlen);
    float *out_high = mod->getArg(node, args[OUT_HIGH])->allocate(windowlen);
    thArg *inout_last = mod->getArg(node, args[INOUT_LAST]);
    thArg *in_arg = mod->getArg(node, args[IN_ARG]);
    thArg *in_cutoff = mod->getArg(node, args[IN_CUTOFF]);
    thArg *in_res = mod->getArg(node, args[IN_RES]);
    thkSvf f;

    f.ic1 = (*inout_last)[0];
    f.ic2 = (*inout_last)[1];

    float *last = inout_last->allocate(2);

    /* A non-finite state would ring for the life of the note. The filter
       cannot produce one on its own -- that is the point of the form -- so
       this is about what arrives. */
    if (!thIsFinite(f.ic1) || !thIsFinite(f.ic2))
        f.ic1 = f.ic2 = 0;

    float in[windowlen];

    in_arg->getBuffer(in, windowlen);

    /* Silence in and none left ringing is silence out, with no
       coefficients to work out. */
    bool quiet = f.ic1 == 0 && f.ic2 == 0;

    for (unsigned int i = 0; quiet && i < windowlen; i++)
        quiet = in[i] == 0;
    if (quiet)
    {
        memset(out_low, 0, windowlen * sizeof(float));
        memset(out_band, 0, windowlen * sizeof(float));
        memset(out_high, 0, windowlen * sizeof(float));
        return 0;
    }

    /* The coefficients again only where the cutoff or resonance moves: a
       tanf a sample would be most of the filter. A NaN never equals
       itself, so it takes the recompute, where the clamps answer for it. */
    float lastCut = (*in_cutoff)[0], lastRes = (*in_res)[0];
    thkSvf::Ctl c = thkSvf::ctl(lastCut, lastRes, (float)samples);
    /* A knob holding still is read once, not a sample at a time. */
    const bool still = in_cutoff->len() <= 1 && in_res->len() <= 1;

    for (unsigned int i = 0; i < windowlen; i++)
    {
        if (!still)
        {
            const float cut = (*in_cutoff)[i], res = (*in_res)[i];

            if (cut != lastCut || res != lastRes)
            {
                c = thkSvf::ctl(cut, res, (float)samples);
                lastCut = cut;
                lastRes = res;
            }
        }
        out_low[i] = TH_MAX * f.tick(c, in[i] / TH_MAX, out_band[i], out_high[i]);
        out_band[i] *= TH_MAX;
        out_high[i] *= TH_MAX;
    }

    last[0] = f.ic1;
    last[1] = f.ic2;

    return 0;
}
