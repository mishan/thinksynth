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

/* One band of a parametric EQ: a peak, a low shelf or a high shelf, set by
 * a frequency, a gain in dB and a q. Several in a row are an EQ.
 *
 * The integrators are filt::svf's, the trapezoidal state-variable filter
 * (Zavalishin's transform, Andrew Simper's form), and the band is a mix of
 * the input and their two taps:
 *
 *     g = tan(pi * freq / rate), A = 10^(gain / 40), k = 1 / q
 *     peak        k = 1 / (q * A)   out = in + k * (A^2 - 1) * band
 *     low shelf   g = g / sqrt(A)   out = in + k * (A - 1) * band + (A^2 - 1) * low
 *     high shelf  g = g * sqrt(A)   out = A^2 * in + k * (1 - A) * A * band
 *                                         + (1 - A^2) * low
 *
 * which is the cookbook's peaking and shelving biquads (Bristow-Johnson)
 * exactly: the same analog prototypes under the same prewarped bilinear
 * transform. A peak's gain at `freq' is `gain', and a shelf's is half of
 * it there, for every q. What this form buys over the biquad's difference
 * equation is the same as svf's: its state is the integrators' and not the
 * last coefficients' outputs, so a frequency or gain moved every sample
 * moves the response and not the state, where a direct form zippers or,
 * transposed, can run away.
 *
 * At 0 dB A is exactly 1 and every term but the input's is exactly 0, so
 * the band is a wire, bit for bit (a -0 aside), whatever the other args
 * are doing.
 *
 * A SHELF'S Q is its pole pair's and its zero pair's, and above 0.707 it
 * does not make the shelf steeper: it overshoots on one side of the corner
 * and dips on the other: on a 24 dB shelf, 1.1 dB at a q of 1 and 5.9 at
 * 2, and at 18 a +48 dB spike. So a shelf's q stops at 2,
 * thkEq::SHELFQMAX, room for the bump a shelf is sometimes wanted with and
 * none for a resonance. A peak's goes to 18.
 *
 * THE FLOOR is 10 Hz and not 0. At g = 0 the integrators stop: a band
 * swept to 0 while it rang would hold its last value as an offset for as
 * long as it stayed there.
 *
 * The input is not clamped to full scale, as svf's is, since a band at 0 dB
 * would then not be a wire, and the output is not clipped: a +24 dB boost
 * of a loud signal is over full scale, and the headroom is the graph's.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

#include "eq.h"

enum {IN_ARG, IN_TYPE, IN_FREQ, IN_GAIN, IN_Q, OUT_ARG, INOUT_LAST};
std::atomic<int> args[INOUT_LAST + 1];

static const char desc[] = "One EQ band: a peak, a low shelf or a high shelf";
thPlugin::State    mystate = thPlugin::ACTIVE;

static const char *const types[] = { "Peak", "Low shelf", "High shelf" };

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

    args[IN_TYPE] = plugin->regArg("type", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_TYPE], "Which band; changed mid-note, it clicks");
    plugin->setArgValues(args[IN_TYPE], types,
                         (int)(sizeof(types) / sizeof(types[0])));

    args[IN_FREQ] = plugin->regArg("freq", thPlugin::ARG_IN);
    plugin->setArgSmooth(args[IN_FREQ], TH_SMOOTH_MS);
    plugin->setArgDesc(args[IN_FREQ],
                       "A peak's center, a shelf's half-gain point; 10 Hz "
                       "to 0.49 of the sample rate");
    plugin->setArgUnits(args[IN_FREQ], "Hz");

    args[IN_GAIN] = plugin->regArg("gain", thPlugin::ARG_IN);
    plugin->setArgSmooth(args[IN_GAIN], TH_SMOOTH_MS);
    plugin->setArgDesc(args[IN_GAIN], "Boost or cut; at 0 the band is a wire");
    plugin->setArgRange(args[IN_GAIN], -thkEq::GAINMAX, thkEq::GAINMAX);
    plugin->setArgUnits(args[IN_GAIN], "dB");

    args[IN_Q] = plugin->regArg("q", thPlugin::ARG_IN);
    plugin->setArgSmooth(args[IN_Q], TH_SMOOTH_MS);
    plugin->setArgDesc(args[IN_Q],
                       "A peak's narrowness, to 18; a shelf's bump at its "
                       "corner, plain at 0.707, to 2");
    plugin->setArgRange(args[IN_Q], thkEq::QMIN, thkEq::QMAX);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The signal through the band; not clipped");
    plugin->setArgRange(args[OUT_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[OUT_ARG], "full scale");

    args[INOUT_LAST] = plugin->regArg("last", thPlugin::ARG_STATE);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out = mod->getArg(node, args[OUT_ARG])->allocate(windowlen);
    thArg *inout_last = mod->getArg(node, args[INOUT_LAST]);
    thArg *in_type = mod->getArg(node, args[IN_TYPE]);
    thArg *in_freq = mod->getArg(node, args[IN_FREQ]);
    thArg *in_gain = mod->getArg(node, args[IN_GAIN]);
    thArg *in_q = mod->getArg(node, args[IN_Q]);
    const thArgReader in(mod->getArg(node, args[IN_ARG])), type(in_type),
                      freq(in_freq), gain(in_gain), q(in_q);
    thkEq eq;

    eq.ic1 = (*inout_last)[0];
    eq.ic2 = (*inout_last)[1];

    float *last = inout_last->allocate(2);

    /* The filter cannot make a non-finite state of its own; this is about
       one that arrives. */
    if (!thIsFinite(eq.ic1) || !thIsFinite(eq.ic2))
        eq.ic1 = eq.ic2 = 0;

    /* Silence in and nothing ringing is silence out, with no coefficients
       to work out. */
    bool quiet = eq.ic1 == 0 && eq.ic2 == 0;

    for (unsigned int i = 0; quiet && i < windowlen; i++)
        quiet = in[i] == 0;
    if (quiet)
    {
        memset(out, 0, windowlen * sizeof(float));
        last[0] = last[1] = 0;
        return 0;
    }

    /* The coefficients again only when what makes them changes, compared
       as bits: the same bits make the same band, a NaN's included. */
    float held[4] = { type[0], freq[0], gain[0], q[0] };
    thkEq::Ctl c = thkEq::ctl(held[0], held[1], held[2], held[3], (float)samples);
    const bool still = in_type->len() <= 1 && in_freq->len() <= 1 &&
                       in_gain->len() <= 1 && in_q->len() <= 1;

    for (unsigned int i = 0; i < windowlen; i++)
    {
        if (!still)
        {
            const float now[4] = { type[i], freq[i], gain[i], q[i] };

            if (memcmp(now, held, sizeof(held)))
            {
                memcpy(held, now, sizeof(held));
                c = thkEq::ctl(held[0], held[1], held[2], held[3], (float)samples);
            }
        }
        out[i] = TH_MAX * eq.tick(c, in[i] / TH_MAX);
    }

    last[0] = eq.ic1;
    last[1] = eq.ic2;

    return 0;
}
