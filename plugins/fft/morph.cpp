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

/* Spectral morph: one sound's spectrum wearing another's shape.
 *
 * On plugins/fft/stft.h's running transform, a frame late. `in' is the
 * carrier, the sound that is heard: its phases, so its pitch and the
 * timing of its transients, are the output's. `side' is the modulator,
 * whose shape it takes on. The side has a ring of its own, written in
 * step with the carrier's and read by the same thkStftFrame, so its frame
 * is the same moment windowed alike.
 *
 * Each bin is scaled by the modulator's level over the carrier's, both the
 * power in a band `width' wide about the bin, and raised to `morph':
 *
 *   out = X g^morph,  g^2 = P_side / (P_in + P_side / G^2),
 *
 * so morph 0 is the carrier, 1 the carrier at the modulator's levels, and
 * between them a blend in decibels, where halfway is halfway and not the
 * louder of the two. At `width' 0 the band is the bin, g is |Y| / |X|, and
 * the output is the modulator's own magnitudes on the carrier's phases: the
 * modulator, smeared. Wider, it is the carrier's partials at the
 * modulator's band levels: a vocoder with the transform's resolution, for
 * which `width' wants to be over the carrier's pitch. Each bin is counted
 * as the half bin either side of it, so the knob is continuous from the
 * one sound to the other, with no steps.
 *
 * G, 50 dB, is as far as a band is lifted. A carrier fading under a loud
 * modulator is lifted toward it only until it is 50 dB under, and then
 * fades with it, rather than sounding at the modulator's level until a
 * threshold cuts it. g is held over 1e-6, 120 dB under the carrier: a
 * silent modulator at morph 1 is that much under it, and at 0.01 1.2 dB
 * under, whatever the carrier is.
 *
 * The band sums are kept in double: in float, a loud bass's running sum
 * swamps a quiet band's to nothing, or below.
 *
 * The level follows the modulator where the carrier has energy in every
 * band; a sparse carrier, a sine, under a broad modulator comes out well
 * under it. A modulator with silent gaps gates the output, deeper the
 * higher morph is: at 1, as a vocoder does. A `side' nobody wired passes
 * the carrier through.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

#include "thKernel.h"
#include "stft.h"

#include "thArg.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thSynthTree.h"
#include "thSynth.h"

enum {IN_ARG, IN_SIDE, IN_SIZE, IN_MORPH, IN_WIDTH, IN_MIX, OUT_ARG, OUT_PLAY, INOUT_STATE};
std::atomic<int> args[INOUT_STATE + 1];

static const char desc[] = "Spectral morph: the carrier's spectrum wearing the side's shape";
thPlugin::State    mystate = thPlugin::ACTIVE;

#define MAXN THK_FFT_MAX
#define BINS (MAXN / 2 + 1)

/* 1 / G^2, G the most a band is lifted (50 dB), and the least g^2. */
#define LIFT_INV2 1e-5
#define GAIN_FLOOR2 1e-12

enum {
    S_N, S_POS, S_HOP,
    S_HOLD,         /* samples of `play' left */
    S_SCALARS = 8,
    S_RING = S_SCALARS,
    S_ACC = S_RING + MAXN,
    S_SIDE = S_ACC + MAXN,      /* the modulator's ring, in step with the carrier's */
    S_LEN = S_SIDE + MAXN
};

void module_cleanup (thPlugin *plugin)
{
}

void module_prepare (thNode *node, thSynthTree *mod, unsigned int samples,
                     int voice)
{
    if (voice)
        mod->getArg(node, args[INOUT_STATE])->reserve(S_LEN);
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    /* Built here rather than on the audio thread's first frame. */
    thkFftTwiddles();

    args[IN_ARG] = plugin->regArg("in", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_ARG], "The carrier: what is heard, its pitch and its detail");
    plugin->setArgRange(args[IN_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[IN_ARG], "full scale");
    args[IN_SIDE] = plugin->regArg("side", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_SIDE],
                       "The modulator, whose shape the carrier takes on; unwired, the "
                       "carrier passes through");
    plugin->setArgRange(args[IN_SIDE], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[IN_SIDE], "full scale");
    args[IN_SIZE] = plugin->regArg("size", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_SIZE],
                       "The frame, read when the node starts, and its latency");
    plugin->setArgRange(args[IN_SIZE], 256, MAXN);
    plugin->setArgDefault(args[IN_SIZE], 2048);
    plugin->setArgUnits(args[IN_SIZE], "samples");
    args[IN_MORPH] = plugin->regArg("morph", thPlugin::ARG_IN);
    plugin->setArgSmooth(args[IN_MORPH], TH_SMOOTH_MS);
    plugin->setArgDesc(args[IN_MORPH],
                       "0 the carrier, 1 the carrier at the side's levels; a blend in dB");
    plugin->setArgRange(args[IN_MORPH], 0, 1);
    plugin->setArgDefault(args[IN_MORPH], 1);
    args[IN_WIDTH] = plugin->regArg("width", thPlugin::ARG_IN);
    plugin->setArgSmooth(args[IN_WIDTH], TH_SMOOTH_MS);
    plugin->setArgDesc(args[IN_WIDTH],
                       "The band the levels are taken over: 0 the side's own spectrum, "
                       "wider its envelope on the carrier's partials");
    plugin->setArgRange(args[IN_WIDTH], 0, 4000);
    plugin->setArgDefault(args[IN_WIDTH], 400);
    plugin->setArgUnits(args[IN_WIDTH], "Hz");
    args[IN_MIX] = plugin->regArg("mix", thPlugin::ARG_IN);
    plugin->setArgSmooth(args[IN_MIX], TH_SMOOTH_MS);
    plugin->setArgDesc(args[IN_MIX], "0 the carrier a frame late, 1 the morphed");
    plugin->setArgRange(args[IN_MIX], 0, 1);
    plugin->setArgDefault(args[IN_MIX], 1);
    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The blend, a frame late");
    plugin->setArgRange(args[OUT_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[OUT_ARG], "full scale");
    args[OUT_PLAY] = plugin->regArg("play", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_PLAY], "1 while there is sound still to come out, for the io node's play");
    plugin->setArgRange(args[OUT_PLAY], 0, 1);

    args[INOUT_STATE] = plugin->regArg("state", thPlugin::ARG_STATE);

    return 0;
}

/* Each bin's band power: the mean of p over [k - w/2, k + w/2], bin j
   counting as [j - 1/2, j + 1/2] and the sum linear across it, clipped to
   the spectrum. `sum' is p's running total, bins + 1 long. */
static double bandAt (const double *sum, const float *p, int bins, int k, double w)
{
    if (w <= 1)
        return p[k];

    const double a = fmax(k - w / 2 + 0.5, 0), b = fmin(k + w / 2 + 0.5, (double)bins);
    const int ia = (int)a, ib = (int)b;
    const double at = sum[ia] + (ia < bins ? (a - ia) * p[ia] : 0);
    const double bt = sum[ib] + (ib < bins ? (b - ib) * p[ib] : 0);

    return fmax(bt - at, 0) / (b - a);
}

/* One frame morphed in place, re and im n/2 + 1 bins, the side's frame in
   sre and sim; whether what it made is still heard. */
static bool morphFrame (float *re, float *im, const float *sre, const float *sim, int n,
                        float m, float widthBins)
{
    const int bins = n / 2 + 1;
    float px[BINS], py[BINS], mag[BINS];
    double sx[BINS + 1], sy[BINS + 1];

    sx[0] = sy[0] = 0;
    for (int k = 0; k < bins; k++)
    {
        px[k] = re[k] * re[k] + im[k] * im[k];
        py[k] = sre[k] * sre[k] + sim[k] * sim[k];
        sx[k + 1] = sx[k] + px[k];
        sy[k + 1] = sy[k] + py[k];
    }

    for (int k = 0; k < bins; k++)
    {
        const double bx = bandAt(sx, px, bins, k, widthBins);
        const double by = bandAt(sy, py, bins, k, widthBins);
        const double den = bx + by * LIFT_INV2;
        const float g2 = (float)fmax(den > 0 ? by / den : 0, GAIN_FLOOR2);
        const float g = m == 1 ? sqrtf(g2) : exp2f(0.5f * m * log2f(g2));

        re[k] *= g;
        im[k] *= g;
        mag[k] = sqrtf(re[k] * re[k] + im[k] * im[k]);
    }
    return thkStftHeard(mag, n);
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    const thArgReader in(mod->getArg(node, args[IN_ARG])),
                      side(mod->getArg(node, args[IN_SIDE])),
                      size(mod->getArg(node, args[IN_SIZE])),
                      morph(mod->getArg(node, args[IN_MORPH])),
                      width(mod->getArg(node, args[IN_WIDTH])),
                      mix(mod->getArg(node, args[IN_MIX]));
    float *st = mod->getArg(node, args[INOUT_STATE])->allocate(S_LEN);
    float *out = mod->getArg(node, args[OUT_ARG])->allocate(windowlen);
    float *play = mod->getArg(node, args[OUT_PLAY])->allocate(windowlen);

    /* The graph's own arg: a `side' that points at another node is a
       modulator, and one holding a number is a `side' nobody wired. */
    const thArg *raw_side = node->getArg(args[IN_SIDE]);
    const bool keyed = raw_side != NULL && raw_side->type() == thArg::ARG_POINTER;

    /* A fresh state, or one that is not a number or out of range, starts
       over: the size taken now, and kept, since a new size is a new
       latency. */
    bool bad = false;

    for (int s = 0; s < S_SCALARS; s++)
        bad = bad || !thkFinite(st[s]);
    if (!bad)
    {
        const int sn = st[S_N] != 0 ? (int)st[S_N] : 2048;

        bad = (st[S_N] != 0 && thkStftSize((float)sn) != sn) || st[S_POS] < 0 ||
              st[S_POS] >= sn || st[S_HOP] < 0 || st[S_HOP] >= sn / 4 ||
              st[S_HOLD] < 0 || st[S_HOLD] > 2 * MAXN;
    }
    if (bad)
        memset(st, 0, S_LEN * sizeof(float));
    if (st[S_N] == 0)
        st[S_N] = (float)thkStftSize(size[0]);

    thkStft f = { st + S_RING, st + S_ACC, (int)st[S_N], (int)st[S_POS], (int)st[S_HOP] };
    float *sring = st + S_SIDE;
    const float rate = samples > 0 ? (float)samples : 44100.0f;
    int hold = (int)st[S_HOLD];

    for (unsigned int i = 0; i < windowlen; i++)
    {
        const float mo = keyed ? thkClamp(morph[i], 0, 1) : 0;
        const float w = thkFinite(width[i]) ? thkClamp(width[i], 0, 4000) : 400;
        const float mx = thkClamp(mix[i], 0, 1);

        /* Bounded, so that no finite input can carry a frame's magnitudes
           past a float. */
        const float v = thkClampMag(in[i], 1e6f);
        float dry;

        /* The side's ring takes its sample where the carrier's is about to,
           so that in the frame -- the position moved on by then -- the two
           are read from the same place. */
        sring[f.pos] = keyed ? thkClampMag(side[i], 1e6f) : 0;

        /* A sample is in the frames for a frame after it arrives, and each
           comes out over the frame after it; and a frame made from a
           carrier under the floor, lifted toward a loud side, sounds for
           the frame after it too. */
        if (fabsf(v) > THK_STFT_PLAY_FLOOR)
            hold = 2 * f.n;

        const float wet = f.tick(v, dry, [&](float *re, float *im, int n) {
            if (mo == 0)
                return;

            float sre[BINS], sim[BINS];

            thkStftFrame(sring, f.pos, n, sre, sim);
            if (morphFrame(re, im, sre, sim, n, mo, w * (float)n / rate))
                hold = hold > n ? hold : n;
        });

        out[i] = mx == 0 ? dry : mx == 1 ? wet : (1 - mx) * dry + mx * wet;
        play[i] = hold > 0 ? 1 : 0;
        if (hold > 0)
            hold--;
    }

    st[S_HOLD] = (float)hold;
    st[S_POS] = (float)f.pos;
    st[S_HOP] = (float)f.hop;

    return 0;
}
