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

/* YIN: the pitch of a voice or an instrument, and how sure of it.
 *
 * de Cheveigne and Kawahara's method (JASA 111, 2002). For each lag tau
 * the signal is compared with itself tau later -- the squared difference
 * summed over a window -- and the lag at which it is most like itself is
 * the period. Divided by the running mean of the differences so far, the
 * difference is 1 for a lag that matches no better than average and near
 * 0 for the period, so a fixed threshold picks the first good dip rather
 * than the deepest one, which would be an octave down. A parabola through
 * the dip and its neighbours puts the period between samples.
 *
 * Counting zero crossings, as analysis::pitch does, reads a saw's or a
 * voice's harmonics as the pitch; this does not.
 *
 * The input is averaged four samples at a time first, so the comparison
 * runs at a quarter of the rate: the pitch of anything a person sings or
 * plays sits far below that Nyquist, and it costs a sixteenth. It is
 * measured every 64 of those samples (5.8 ms at 44.1 kHz), counted by
 * sample, so it lands at the same instants at any window length.
 *
 * `out' is the period's frequency, or 0 where no lag is good enough --
 * breath, a consonant, silence -- and `clarity' is one minus the dip's
 * depth, 1 for a pure periodic tone.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

enum { IN_ARG, IN_MIN, IN_MAX, IN_THRESHOLD, OUT_ARG, OUT_CLARITY,
       INOUT_BUFFER, INOUT_STATE };

std::atomic<int> args[INOUT_STATE + 1];

static const char desc[] = "YIN (the pitch of a voice or an instrument)";
thPlugin::State    mystate = thPlugin::ACTIVE;

#define DECIMATE 4
#define HOP 64

/* The decimator's sum and count, the ring's write position, decimated
   samples to the next measurement, and the two outputs. */
enum { S_SUM, S_COUNT, S_WRITE, S_HOP, S_FREQ, S_CLARITY, S_LEN };

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
    args[IN_MIN] = plugin->regArg("min", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_MIN], "The lowest pitch looked for; 70 at 0");
    plugin->setArgDefault(args[IN_MIN], 70);
    plugin->setArgUnits(args[IN_MIN], "Hz");
    args[IN_MAX] = plugin->regArg("max", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_MAX], "The highest pitch looked for; 1000 "
                                     "at 0");
    plugin->setArgDefault(args[IN_MAX], 1000);
    plugin->setArgUnits(args[IN_MAX], "Hz");
    args[IN_THRESHOLD] = plugin->regArg("threshold", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_THRESHOLD],
                       "How deep a dip has to be to count as the period; "
                       "0.15 at 0");
    plugin->setArgDefault(args[IN_THRESHOLD], 0.15f);
    plugin->setArgRange(args[IN_THRESHOLD], 0.01f, 0.5f);
    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The pitch, or 0 where there is none");
    plugin->setArgUnits(args[OUT_ARG], "Hz");
    args[OUT_CLARITY] = plugin->regArg("clarity", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_CLARITY],
                       "How periodic the input is: 1 a pure tone, 0 noise");
    plugin->setArgRange(args[OUT_CLARITY], 0, 1);
    args[INOUT_BUFFER] = plugin->regArg("buffer", thPlugin::ARG_STATE);
    args[INOUT_STATE] = plugin->regArg("state", thPlugin::ARG_STATE);

    return 0;
}

/* The lags the difference is taken over, and the window it is summed
   over, at the decimated rate. */
static void lags (float min, float max, float rate, unsigned *lo,
                  unsigned *hi)
{
    if (!thIsFinite(min) || min <= 0)
        min = 70;

    if (!thIsFinite(max) || max <= 0)
        max = 1000;

    min = thClampArg(min, 20, rate / 4);
    max = thClampArg(max, min, rate / 4);

    *hi = (unsigned)ceilf(rate / min);
    *lo = (unsigned)floorf(rate / max);

    if (*lo < 2)
        *lo = 2;
}

/* One measurement over the newest `win + hi' samples of the ring:
   returns the frequency, 0 for none, and sets the clarity. */
static float measure (const float *ring, unsigned len, unsigned w,
                      unsigned lo, unsigned hi, float threshold, float rate,
                      float *d, float *clarity)
{
    const unsigned win = hi;
    const unsigned start = (w + len - win - hi) % len;
    float sum = 0;

    d[0] = 1;

    for (unsigned tau = 1; tau <= hi; tau++)
    {
        float diff = 0;

        for (unsigned j = 0; j < win; j++)
        {
            const float a = ring[(start + j) % len];
            const float b = ring[(start + j + tau) % len];

            diff += (a - b) * (a - b);
        }

        sum += diff;
        d[tau] = sum > 0 ? diff * tau / sum : 1;
    }

    unsigned best = 0;

    for (unsigned tau = lo; tau <= hi; tau++)
        if (d[tau] < threshold)
        {
            while (tau + 1 <= hi && d[tau + 1] < d[tau])
                tau++;

            best = tau;
            break;
        }

    if (best == 0)
    {
        float lowest = 2;

        for (unsigned tau = lo; tau <= hi; tau++)
            if (d[tau] < lowest)
                lowest = d[tau];

        *clarity = fmaxf(0, 1 - lowest);
        return 0;
    }

    *clarity = fmaxf(0, 1 - d[best]);

    /* The dip's own minimum, between samples. */
    float period = (float)best;

    if (best > lo && best < hi)
    {
        const float a = d[best - 1], b = d[best], c = d[best + 1];
        const float den = a - 2 * b + c;

        if (den > 0)
            period += 0.5f * (a - c) / den;
    }

    return rate / period;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    thArg *in_arg = mod->getArg(node, args[IN_ARG]);
    thArg *in_min = mod->getArg(node, args[IN_MIN]);
    thArg *in_max = mod->getArg(node, args[IN_MAX]);
    thArg *in_threshold = mod->getArg(node, args[IN_THRESHOLD]);
    thArg *inout_state = mod->getArg(node, args[INOUT_STATE]);
    thArg *inout_buffer = mod->getArg(node, args[INOUT_BUFFER]);

    float st[S_LEN];

    for (int k = 0; k < S_LEN; k++)
        st[k] = (unsigned)k < inout_state->len() ? (*inout_state)[k] : 0;

    const float rate = (float)samples / DECIMATE;
    unsigned lo, hi;

    /* The ring is sized for the lowest pitch this rate allows, so a
       moving `min' never resizes it. */
    lags(20, 20, rate, &lo, &hi);

    const unsigned len = 2 * hi + 2;
    float *state = inout_state->allocate(S_LEN);
    float *buf = inout_buffer->allocate(len + hi + 1);
    float *ring = buf, *d = buf + len;
    float *out = mod->getArg(node, args[OUT_ARG])->allocate(windowlen);
    float *out_clarity =
        mod->getArg(node, args[OUT_CLARITY])->allocate(windowlen);
    unsigned w = (unsigned)st[S_WRITE];

    if (w >= len)
        w = 0;

    for (unsigned i = 0; i < windowlen; i++)
    {
        const float x = (*in_arg)[i];

        st[S_SUM] += thIsFinite(x) ? x : 0;

        if (++st[S_COUNT] >= DECIMATE)
        {
            ring[w] = st[S_SUM] / DECIMATE;
            w = (w + 1) % len;
            st[S_SUM] = 0;
            st[S_COUNT] = 0;

            if (++st[S_HOP] >= HOP)
            {
                float threshold = (*in_threshold)[i];

                if (!thIsFinite(threshold) || threshold <= 0)
                    threshold = 0.15f;

                lags((*in_min)[i], (*in_max)[i], rate, &lo, &hi);
                st[S_FREQ] = measure(ring, len, w, lo, hi,
                                     thClampArg(threshold, 0.01f, 0.5f),
                                     rate, d, &st[S_CLARITY]);
                st[S_HOP] = 0;
            }
        }

        out[i] = st[S_FREQ];
        out_clarity[i] = st[S_CLARITY];
    }

    st[S_WRITE] = (float)w;
    memcpy(state, st, sizeof(st));

    return 0;
}
