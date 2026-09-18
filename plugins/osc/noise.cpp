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

/* White, pink and brown, on a `color' arg.
 *
 * Every drum in the tree is two squares ring-modulated through white noise,
 * because white is the one noise there is: osc::static draws a uniform number
 * and holds it for `sample' samples, which is white when `sample' is 0 and a
 * coarser white when it is not. A snare wants pink and a floor tom wants
 * brown, and neither is reachable by holding a white one for longer.
 *
 *   white  flat.
 *   pink   3 dB an octave down -- the three-pole approximation, Paul
 *          Kellett's coefficients, which is within a third of a decibel of
 *          the real slope from about 10 Hz to about 20 kHz.
 *   brown  6 dB an octave down -- a leaky integrator, which is the integral
 *          of white with the DC that an exact integral would accumulate
 *          bled off.
 *
 * `amp' is a peak for all three. White is uniform and reaches it exactly;
 * pink and brown are sums of many draws and have no bound at all, so they are
 * scaled to put five standard deviations at `amp' -- about one sample in
 * three million is clipped -- and that costs them nine decibels against a
 * white at the same `amp'. The alternative is matching the three in level and
 * clipping a pink one eight percent of the time, which is a distortion nobody
 * asked for and no graph can undo. A graph that wants them equally loud says
 * so in its `amp'.
 *
 * The draw is one per sample whatever the color is, so `color' can move
 * without the stream moving with it.
 *
 * Determinism is plugins/osc/noiseslot.h: one generator per synth, claimed in
 * module_init, starting where every synth's starts. Two fresh synths render
 * the same noise, which is what dspcheck's render-twice comparison and the
 * wasm-against-native gate both rest on.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

#include "noiseslot.h"

static const char desc[] = "White, pink or brown noise";
thPlugin::State    mystate = thPlugin::ACTIVE;

/* Kellett's economical pink filter: three one-pole sections summed, plus a
   little of the white itself for the top octave. */
#define PINK_P0 0.99765f
#define PINK_P1 0.96300f
#define PINK_P2 0.57000f
#define PINK_G0 0.0990460f
#define PINK_G1 0.2965164f
#define PINK_G2 1.0526913f
#define PINK_GW 0.1848f

/* Its output's standard deviation for a white input uniform over -1..1,
   measured over twenty million samples: 1.720370, against the white's own
   0.577270. Analytically it is a sum of correlated terms and not worth
   writing out; what matters is that it is a constant of the coefficients
   above, so it moves only if they do. */
#define PINK_SIGMA 1.720370f

/* The leaky integrator's pole. A corner at rate*(1 - p)/2pi -- 35 Hz at
   44.1k -- below which the slope flattens to nothing rather than running
   away to DC, and above which it is the 6 dB an octave that makes it brown.
   Its standard deviation is exact: sqrt((1 - p)/(1 + p)/3) for a white input
   uniform over -1..1, which is 0.0289044. */
#define BROWN_P 0.995f
#define BROWN_SIGMA 0.0289044f

/* Where `amp' sits on a color with no peak of its own. Five sigma is the
   largest excursion in twenty million samples of either; a Gaussian is past
   it one sample in three and a half million. */
#define NOISE_SIGMAS 5.0f

static const char *const colors[] = { "White", "Pink", "Brown" };

enum {OUT_ARG, IN_COLOR, IN_AMP, INOUT_LAST};
int args[INOUT_LAST + 1];

void module_cleanup (thPlugin *plugin)
{
    thNoiseRelease(plugin);
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    thNoiseClaim(plugin);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The noise");
    plugin->setArgRange(args[OUT_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[OUT_ARG], "full scale");

    args[IN_COLOR] = plugin->regArg("color", thPlugin::ARG_IN);
    /* Read `switch ((int)buf_color[i])', so it is a selector: 1.4 is pink
       and so is 1.9. Naming the values is what lets a control driving it be
       a list of three rather than a slider two thirds of whose travel does
       nothing. */
    plugin->setArgDesc(args[IN_COLOR], "Which noise");
    plugin->setArgValues(args[IN_COLOR], colors,
                         (int)(sizeof(colors) / sizeof(colors[0])));

    args[IN_AMP] = plugin->regArg("amp", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_AMP],
                       "Peak amplitude; pink and brown are about nine "
                       "decibels quieter than white at the same peak");
    plugin->setArgRange(args[IN_AMP], 0, TH_MAX);
    plugin->setArgUnits(args[IN_AMP], "full scale");
    /* `if (amp == 0) amp = TH_MAX' below -- the same zero case osc::simple
       has, declared so the reference and the editor's defaults agree with
       the callback. */
    plugin->setArgDefault(args[IN_AMP], TH_MAX);

    /* The two filters' history: three pink poles and the brown integrator.
       Per node, so two noise nodes of different colors do not share a
       filter, and across windows, so neither restarts every 1024 samples. */
    args[INOUT_LAST] = plugin->regArg("last", thPlugin::ARG_STATE);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out;
    float *out_last;
    thArg *in_color, *in_amp;
    thArg *out_arg;
    thArg *inout_last;
    unsigned int i;
    float b0, b1, b2, brown;

    out_arg = mod->getArg(node, args[OUT_ARG]);
    inout_last = mod->getArg(node, args[INOUT_LAST]);

    b0 = (*inout_last)[0];
    b1 = (*inout_last)[1];
    b2 = (*inout_last)[2];
    brown = (*inout_last)[3];
    out_last = inout_last->allocate(4);

    /* Both filters feed back, so one non-finite sample would be read back
       for the life of the note. Nothing here can produce one -- the input is
       a bounded draw and every pole is inside the unit circle -- so this is
       about a buffer that has been somewhere else. */
    if (!thIsFinite(b0) || !thIsFinite(b1) || !thIsFinite(b2) ||
        !thIsFinite(brown))
    {
        b0 = b1 = b2 = brown = 0;
    }

    out = out_arg->allocate(windowlen);

    in_color = mod->getArg(node, args[IN_COLOR]);
    in_amp = mod->getArg(node, args[IN_AMP]);

    /* This synth's generator, held in a local for the window and written
       back once at the end. */
    thNoiseSlot *slot = thNoiseSlotFor(node->plugin());
    unsigned s = (slot != NULL) ? slot->state : 0;

    for (i = 0; i < windowlen; i++)
    {
        unsigned r;
        float w, y, amp;

        if (slot != NULL) {
            s = thNoiseStep(s);
            r = thNoiseBits(s);
        } else {
            r = thNoiseSharedBits();
        }

        /* 2^31, not RAND_MAX+1: r is 31 bits everywhere, and RAND_MAX is
           32767 on Windows. */
        w = (float)(TH_RANGE * (r / 2147483648.0) + TH_MIN);

        /* Both run every sample whatever the color is. A filter that only
           ran while it was selected would take a second to settle every time
           `color' moved, and a pink node next to a brown one would then
           disagree about what the same stream sounds like. */
        b0 = PINK_P0 * b0 + w * PINK_G0;
        b1 = PINK_P1 * b1 + w * PINK_G1;
        b2 = PINK_P2 * b2 + w * PINK_G2;
        brown = BROWN_P * brown + (1.0f - BROWN_P) * w;

        /* Clamped before the cast: converting a NaN or a large float to an
           int is undefined, and `color' is an arg like any other -- a graph
           may drive it from a node. */
        switch ((int)thClampArg((*in_color)[i], 0.0f,
                                (float)(sizeof(colors) / sizeof(colors[0]) - 1)))
        {
        case 1:
            y = (b0 + b1 + b2 + w * PINK_GW) /
                (PINK_SIGMA * NOISE_SIGMAS);
            break;

        case 2:
            y = brown / (BROWN_SIGMA * NOISE_SIGMAS);
            break;

        default:
            y = w;
            break;
        }

        amp = thClampArg((*in_amp)[i], 0.0f, TH_MAX);

        if (amp == 0)
            amp = TH_MAX;

        y *= amp;

        /* The one in three million that five sigma leaves over, and any
           clipping a graph asked for by driving `amp' from something. */
        out[i] = (y > amp) ? amp : ((y < -amp) ? -amp : y);
    }

    if (slot != NULL)
        slot->state = s;

    out_last[0] = b0;
    out_last[1] = b1;
    out_last[2] = b2;
    out_last[3] = brown;

    return 0;
}
