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

/* A formant filter: five band-passes where a voice puts its vowel.
 *
 * A vowel is not a pitch but a shape the throat gives any pitch -- a
 * handful of resonances, the formants, which stay where they are while
 * the note moves under them. `a' has its first two near 650 and 1080 Hz,
 * `i' near 290 and 1870, and the ear tells the vowels apart by those two
 * alone; the three above them are what makes it a voice and not a
 * whistle. Five band-passes in parallel, each at a formant's frequency
 * and width and weighted by its level, are those resonances, and a
 * bright source through them -- a pad, a saw, noise -- is a choir.
 *
 * `vowel' runs 0 to 4 through a, e, i, o, u, and anywhere between two is
 * those two's frequencies, widths and levels interpolated, so a vowel
 * swept is a mouth moving and not a crossfade between two filters.
 * `gender' scales every formant together, an octave's quarter up at 1
 * and down at -1: a smaller throat puts its resonances higher, which is
 * most of what separates a soprano's `a' from a tenor's.
 *
 * The table is Csound's tenor formants (the Csound manual's appendix,
 * after Peterson and Barney's measurements), five a vowel.
 *
 * Each band-pass is Zavalishin's trapezoidal state-variable filter, which
 * stays stable to Nyquist at any width where the Chamberlin one in
 * filt::svf needs its cutoff clamped, and its band output is normalized
 * to unity at the center, so a formant's level is its level. The
 * coefficients are worked out every sample, since `vowel' is a thing a
 * graph sweeps.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

enum {IN_ARG, IN_VOWEL, IN_GENDER, OUT_ARG, INOUT_STATE};
int args[INOUT_STATE + 1];

static const char desc[] = "Vowel (five formants, a to u)";
thPlugin::State    mystate = thPlugin::ACTIVE;

#define VOWELS 5
#define FORMANTS 5

/* Hz, dB and Hz: center, level and bandwidth, a e i o u. */
static const float formantHz[VOWELS][FORMANTS] = {
    { 650, 1080, 2650, 2900, 3250 },
    { 400, 1700, 2600, 3200, 3580 },
    { 290, 1870, 2800, 3250, 3540 },
    { 400,  800, 2600, 2800, 3000 },
    { 350,  600, 2700, 2900, 3300 },
};

static const float formantDb[VOWELS][FORMANTS] = {
    { 0,  -6,  -7,  -8, -22 },
    { 0, -14, -12, -14, -20 },
    { 0, -15, -18, -20, -30 },
    { 0, -10, -12, -12, -26 },
    { 0, -20, -17, -14, -26 },
};

static const float formantBw[VOWELS][FORMANTS] = {
    { 80, 90, 120, 130, 140 },
    { 70, 80, 100, 120, 120 },
    { 40, 90, 100, 120, 120 },
    { 40, 80, 100, 120, 120 },
    { 40, 60, 100, 120, 120 },
};

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
    args[IN_VOWEL] = plugin->regArg("vowel", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_VOWEL],
                       "0 a, 1 e, 2 i, 3 o, 4 u, and between two the mouth "
                       "moving from one to the other");
    plugin->setArgRange(args[IN_VOWEL], 0, 4);
    args[IN_GENDER] = plugin->regArg("gender", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_GENDER],
                       "Every formant scaled together: 1 a quarter octave "
                       "up, -1 one down");
    plugin->setArgRange(args[IN_GENDER], -1, 1);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    /* No range: five resonances at unity each can sum past the input at a
       frequency two of them share. */
    plugin->setArgDesc(args[OUT_ARG], "The five formants, summed");
    plugin->setArgUnits(args[OUT_ARG], "full scale");

    /* Two integrator states a formant. */
    args[INOUT_STATE] = plugin->regArg("state", thPlugin::ARG_STATE);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    thArg *in_arg = mod->getArg(node, args[IN_ARG]);
    thArg *in_vowel = mod->getArg(node, args[IN_VOWEL]);
    thArg *in_gender = mod->getArg(node, args[IN_GENDER]);
    thArg *out_arg = mod->getArg(node, args[OUT_ARG]);
    thArg *inout_state = mod->getArg(node, args[INOUT_STATE]);

    float *out = out_arg->allocate(windowlen);
    float *state = inout_state->allocate(2 * FORMANTS);

    /* A formant no higher than this fraction of the rate: the prewarped
       tan() runs away at Nyquist, and a formant above 20 kHz is no part
       of any vowel. */
    const double top = 0.45 * samples;

    for (unsigned int i = 0; i < windowlen; i++)
    {
        const float vowel = thClampArg((*in_vowel)[i], 0, VOWELS - 1);
        const float gender = thClampArg((*in_gender)[i], -1, 1);
        const int lo = vowel >= VOWELS - 1 ? VOWELS - 2 : (int)vowel;
        const float t = vowel - (float)lo;
        const double scale = pow(2.0, gender / 4.0);
        const float x = thIsFinite((*in_arg)[i]) ? (*in_arg)[i] : 0;
        float y = 0;

        for (int f = 0; f < FORMANTS; f++)
        {
            const double hz = fmin(top, scale * (formantHz[lo][f] +
                (formantHz[lo + 1][f] - formantHz[lo][f]) * t));
            const double bw = scale * (formantBw[lo][f] +
                (formantBw[lo + 1][f] - formantBw[lo][f]) * t);
            const double db = formantDb[lo][f] +
                (formantDb[lo + 1][f] - formantDb[lo][f]) * t;
            const double g = tan(M_PI * hz / samples);
            const double k = bw / hz;               /* 1 / Q */
            const double a1 = 1 / (1 + g * (g + k));
            float *s1 = &state[2 * f], *s2 = &state[2 * f + 1];

            const double v1 = a1 * (*s1 + g * (x - *s2));
            const double v2 = *s2 + g * v1;

            *s1 = (float)(2 * v1 - *s1);
            *s2 = (float)(2 * v2 - *s2);

            /* Denormal-free and NaN-free, since the states are the
               filter's memory for as long as the voice lasts. */
            if (!(fabsf(*s1) > 1e-30f))
                *s1 = 0;

            if (!(fabsf(*s2) > 1e-30f))
                *s2 = 0;

            /* The band output is v1; times k it is unity at the center. */
            y += (float)(pow(10.0, db / 20.0) * k * v1);
        }

        out[i] = y;
    }

    return 0;
}
