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

/* Spectral shift: every partial moved by `ratio' and then by `offset'
 * hertz, the phase of each carried on so that a chord shifts as a chord.
 *
 * On plugins/fft/stft.h's running transform, a frame late. Each frame,
 * its peaks -- the largest bin within two -- each own the bins nearest
 * them, a partial's lobe. A peak's true frequency w, in bins, is its bin
 * plus how far its phase has turned since the last frame beyond what the
 * bin itself turns. It is sent to f = w ratio + offset; its whole lobe
 * moves the whole number of bins D nearest f - w, so that the lobe lands
 * within half a bin of f, and each bin keeps its phase against the peak's
 * (identity phase locking), turned as the peak's turns at f.
 *
 * THE PHASE GOES ON FROM WHAT WAS LAST MADE, at the bin each moved bin
 * lands in: the whole synthesized spectrum is kept, so a partial that
 * moves a bin finds the phase its neighbor left -- a Hann lobe's bins
 * alternate by half a turn, which is what a moved peak needs. Kept only
 * at peaks, a chirp or vibrato or noise came out uncorrelated with itself
 * even at ratio 1; kept whole, ratio 1 is the input to float error.
 *
 * A lobe left d bins from the frequency its phase turns at overlap-adds
 * to G(d) of itself (GAIN below, worked out from the window: 1 at 0,
 * 0.905 at half a bin), so each lobe is divided by it.
 *
 * FORMANTS. `formant' 1 keeps the spectrum's envelope where it was: an
 * envelope drawn through the peaks within 40 dB of the frame's loudest,
 * in log magnitude, and each moved bin scaled by the envelope where it
 * lands over the envelope where it was, held to 24 dB either way --
 * drawn through every peak, a noise floor's maximum between two
 * harmonics was lifted to a harmonic's level.
 *
 * A partial sent under 0 Hz comes back above it, reflected, as a
 * frequency shifter's does; over Nyquist it is dropped. Two that land on
 * a bin add.
 *
 * LIMITS. A partial under about two bins -- 344 Hz at 256, 43 Hz at 2048 --
 * is not moved cleanly, its lobe being two images about DC; a bass wants
 * 2048 or more. Between a dense tone's harmonics it leaves about 36 dB
 * down what its lobes, cut apart and moved by different whole bins, no
 * longer cancel. An attack smears over the frame, as in any phase vocoder.
 * Noise comes out about 2.5 dB down once shifted, its bins' phases locked
 * to peaks that are not partials.
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

enum {IN_ARG, IN_SIZE, IN_RATIO, IN_OFFSET, IN_FORMANT, IN_MIX, OUT_ARG, OUT_PLAY, INOUT_STATE};

std::atomic<int> args[INOUT_STATE + 1];

static const char desc[] = "Spectral shift: every partial moved by a ratio and an offset";
thPlugin::State    mystate = thPlugin::ACTIVE;

#define MAXN THK_FFT_MAX
#define BINS (MAXN / 2 + 1)

/* G(d) at d = 0, 1/64 .. 1/2 bins: the overlap-added level of a lobe d
   bins from the frequency its phase turns at, a hop a quarter frame
   under a periodic Hann in and out, averaged over a hop. The same at
   every size. */
static const float GAIN[33] = {
    1.00000f, 0.99990f, 0.99961f, 0.99913f, 0.99846f, 0.99759f, 0.99653f, 0.99529f,
    0.99385f, 0.99222f, 0.99040f, 0.98839f, 0.98620f, 0.98382f, 0.98126f, 0.97851f,
    0.97558f, 0.97247f, 0.96918f, 0.96572f, 0.96208f, 0.95826f, 0.95428f, 0.95012f,
    0.94579f, 0.94130f, 0.93665f, 0.93183f, 0.92686f, 0.92173f, 0.91644f, 0.91101f,
    0.90542f
};

static float gainAt (float d)
{
    const float x = fminf(fabsf(d), 0.5f) * 64;
    const int i = x >= 32 ? 31 : (int)x;

    return GAIN[i] + (GAIN[i + 1] - GAIN[i]) * (x - (float)i);
}

/* A formant's gain is held to this either way, 24 dB in log2 units. */
#define FORMANT_LIMIT 3.986f

enum {
    S_N, S_POS, S_HOP, S_HOLD, S_STARTED,
    S_SCALARS = 8,
    S_RING = S_SCALARS,
    S_ACC = S_RING + MAXN,
    S_XR = S_ACC + MAXN, S_XI = S_XR + BINS,        /* the last frame in */
    S_YR = S_XI + BINS, S_YI = S_YR + BINS,         /* the phase last made at each bin */
    S_LEN = S_YI + BINS
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
    plugin->setArgDesc(args[IN_ARG], "Signal in");
    plugin->setArgRange(args[IN_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[IN_ARG], "full scale");

    args[IN_SIZE] = plugin->regArg("size", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_SIZE],
                       "The frame, read when the node starts, and its latency");
    plugin->setArgRange(args[IN_SIZE], 256, MAXN);
    plugin->setArgDefault(args[IN_SIZE], 2048);
    plugin->setArgUnits(args[IN_SIZE], "samples");

    args[IN_RATIO] = plugin->regArg("ratio", thPlugin::ARG_IN);
    plugin->setArgSmooth(args[IN_RATIO], TH_SMOOTH_MS);
    plugin->setArgDesc(args[IN_RATIO],
                       "How far every partial moves: 2 is an octave up, 0.5 one down; 0 is 1");
    plugin->setArgRange(args[IN_RATIO], 0.25, 4);
    plugin->setArgDefault(args[IN_RATIO], 1);

    args[IN_OFFSET] = plugin->regArg("offset", thPlugin::ARG_IN);
    plugin->setArgSmooth(args[IN_OFFSET], TH_SMOOTH_MS);
    plugin->setArgDesc(args[IN_OFFSET],
                       "Hertz added after the ratio, which makes partials inharmonic");
    plugin->setArgRange(args[IN_OFFSET], -2000, 2000);
    plugin->setArgUnits(args[IN_OFFSET], "Hz");

    args[IN_FORMANT] = plugin->regArg("formant", thPlugin::ARG_IN);
    plugin->setArgSmooth(args[IN_FORMANT], TH_SMOOTH_MS);
    plugin->setArgDesc(args[IN_FORMANT],
                       "0 the formants move with the pitch, 1 they stay where they were");
    plugin->setArgRange(args[IN_FORMANT], 0, 1);

    args[IN_MIX] = plugin->regArg("mix", thPlugin::ARG_IN);
    plugin->setArgSmooth(args[IN_MIX], TH_SMOOTH_MS);
    plugin->setArgDesc(args[IN_MIX], "0 the input a frame late, 1 the shifted");
    plugin->setArgRange(args[IN_MIX], 0, 1);

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

/* One frame moved: re and im in, and out, n/2 + 1 bins. */
static void shiftFrame (float *re, float *im, int n, float *st, float ratio, float offBins,
                        float formant)
{
    const int bins = n / 2 + 1, h = n / 4;
    float *xr = st + S_XR, *xi = st + S_XI, *yr = st + S_YR, *yi = st + S_YI;
    const bool started = st[S_STARTED] > 0;
    float mag[BINS], loge[BINS], outr[BINS], outi[BINS], most[BINS], pr[BINS], pi[BINS];
    int peaks[BINS / 2 + 1], np = 0;
    float loud = 0;

    for (int k = 0; k < bins; k++)
    {
        const float m = sqrtf(re[k] * re[k] + im[k] * im[k]);

        mag[k] = m < 1e-30f ? 0 : m;
        loud = fmaxf(loud, mag[k]);
        outr[k] = outi[k] = most[k] = 0;
        pr[k] = yr[k];
        pi[k] = yi[k];
    }

    /* The peaks: the largest within two, the first of a tie. */
    for (int k = 0; k < bins; k++)
    {
        const float m = mag[k];

        if (m == 0 || (k >= 1 && mag[k - 1] >= m) || (k >= 2 && mag[k - 2] >= m) ||
            (k + 1 < bins && mag[k + 1] > m) || (k + 2 < bins && mag[k + 2] > m))
            continue;
        peaks[np++] = k;
    }

    /* The envelope, in log2: through the peaks within 40 dB of the
       loudest, held flat past the first and the last. The loudest bin is
       always a peak, so there is one. */
    bool shaped = false;

    if (formant > 0 && np > 0)
    {
        const float floor = loud * 0.01f;
        int last = -1;

        for (int i = 0; i < np; i++)
        {
            const int p = peaks[i];

            if (mag[p] < floor)
                continue;

            const float lp = log2f(mag[p]);

            if (last < 0)
                for (int k = 0; k <= p; k++)
                    loge[k] = lp;
            else
            {
                const float ll = loge[last];

                for (int k = last + 1; k <= p; k++)
                    loge[k] = ll + (lp - ll) * (float)(k - last) / (float)(p - last);
            }
            last = p;
        }
        shaped = last >= 0;
        for (int k = last + 1; shaped && k < bins; k++)
            loge[k] = loge[last];
    }

    for (int i = 0; i < np; i++)
    {
        const int p = peaks[i];
        const int lo = i == 0 ? 0 : (peaks[i - 1] + p) / 2 + 1;
        const int hi = i + 1 == np ? bins - 1 : (p + peaks[i + 1]) / 2;

        /* Its true frequency, from how far it turned since the last frame. */
        float w = (float)p;
        float rr = thkSin2pi((float)p * (float)h / (float)n + 0.25f), ri = thkSin2pi((float)p * (float)h / (float)n);

        if (started)
        {
            const float zr = re[p] * xr[p] + im[p] * xi[p], zi = im[p] * xr[p] - re[p] * xi[p];
            const float zm = sqrtf(zr * zr + zi * zi);
            float dev = thkAtan2Turns(zi, zr) - (float)p * (float)h / (float)n;

            dev -= floorf(dev + 0.5f);
            w = (float)p + dev * (float)n / (float)h;
            if (zm > 0)
            {
                rr = zr / zm;
                ri = zi / zm;
            }
        }

        /* Reflected only once the peak would land under bin 0, and dropped
           only once over the last: a peak at an edge whose true frequency
           is a little past it stays where it is at ratio 1. Mirrored, the
           lobe runs the other way about where it lands. */
        float f = w * ratio + offBins;
        const bool mirror = p + (int)lrintf(f - w) < 0;

        if (mirror)
            f = -f;

        const int D = (int)lrintf(f - (mirror ? -w : w));
        const int tp = mirror ? D - p : p + D;
        const float d = f - (mirror ? (float)D - w : w + (float)D);

        if (tp < 0 || tp >= bins)
            continue;

        /* The peak's phasor, from what was last made where it lands, turned
           as a partial at f turns over a hop; or, new, its own. The turn is
           the one measured, times what f adds to w: at ratio 1 the
           measurement itself, so that nothing builds up from frame to frame
           but float error. */
        const float pm = mag[p];
        float zr = re[p] / pm, zi = (mirror ? -im[p] : im[p]) / pm;
        const float ym = sqrtf(yr[tp] * yr[tp] + yi[tp] * yi[tp]);

        if (started && ym > 0)
        {
            const float t = (w * ratio + offBins - w) * (float)h / (float)n;
            const float c = thkSin2pi(t + 0.25f), s = thkSin2pi(t);
            float tr = rr * c - ri * s, ti = rr * s + ri * c;
            const float ur = yr[tp] / ym, ui = yi[tp] / ym;

            if (mirror)
                ti = -ti;
            zr = ur * tr - ui * ti;
            zi = ur * ti + ui * tr;
        }

        /* Each bin's place against the peak, X_j conj(X_p) / |X_p|, turned
           by the peak's phasor: conjugated where mirrored. */
        const float g = 1 / gainAt(d);
        const float cr = re[p] / pm, ci = -im[p] / pm;

        for (int j = lo; j <= hi; j++)
        {
            const int t = mirror ? D - j : j + D;

            if (t < 0 || t >= bins || mag[j] == 0)
                continue;

            float a = re[j] * cr - im[j] * ci, b = re[j] * ci + im[j] * cr;

            if (mirror)
                b = -b;

            float gain = g;

            if (shaped)
            {
                const float e = thkClamp(formant * (loge[t] - loge[j]), -FORMANT_LIMIT, FORMANT_LIMIT);

                gain *= exp2f(e);
            }
            const float vr = gain * (a * zr - b * zi), vi = gain * (a * zi + b * zr);

            outr[t] += vr;
            outi[t] += vi;

            /* The phase remembered at a bin is the strongest partial's that
               lands there, not the sum's, so that a peak where two lobes
               overlap goes on from its own. */
            const float vm = vr * vr + vi * vi;

            if (vm > most[t])
            {
                most[t] = vm;
                pr[t] = vr;
                pi[t] = vi;
            }
        }
    }

    for (int k = 0; k < bins; k++)
    {
        xr[k] = re[k];
        xi[k] = im[k];
        re[k] = outr[k];
        im[k] = outi[k];
        yr[k] = most[k] > 0 ? pr[k] : 0;
        yi[k] = most[k] > 0 ? pi[k] : 0;
    }
    st[S_STARTED] = 1;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    const thArgReader in(mod->getArg(node, args[IN_ARG])),
                      size(mod->getArg(node, args[IN_SIZE])),
                      ratio(mod->getArg(node, args[IN_RATIO])),
                      offset(mod->getArg(node, args[IN_OFFSET])),
                      formant(mod->getArg(node, args[IN_FORMANT])),
                      mix(mod->getArg(node, args[IN_MIX]));
    float *st = mod->getArg(node, args[INOUT_STATE])->allocate(S_LEN);
    float *out = mod->getArg(node, args[OUT_ARG])->allocate(windowlen);
    float *play = mod->getArg(node, args[OUT_PLAY])->allocate(windowlen);

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
    const float rate = samples > 0 ? (float)samples : 44100.0f;
    int hold = (int)st[S_HOLD];

    for (unsigned int i = 0; i < windowlen; i++)
    {
        const float r = !thkFinite(ratio[i]) || ratio[i] == 0 ? 1 : thkClamp(ratio[i], 0.25f, 4);
        const float off = thkFinite(offset[i]) ? thkClamp(offset[i], -2000, 2000) : 0;
        const float fm = thkClamp(formant[i], 0, 1), m = thkClamp(mix[i], 0, 1);

        /* Bounded, so that no finite input can carry a frame's magnitudes
           past a float. */
        const float v = thkClampMag(in[i], 1e6f);
        float dry;

        /* As fft::gate's: a shift holds nothing past its input, so its
           sound is over once the input is out of every frame -- a sample is
           in them for a frame after it arrives, and each comes out over the
           frame after it. */
        if (fabsf(v) > THK_STFT_PLAY_FLOOR)
            hold = 2 * f.n;

        const float wet = f.tick(v, dry, [&](float *re, float *im, int n) {
            shiftFrame(re, im, n, st, r, off * (float)n / rate, fm);
        });

        out[i] = m == 0 ? dry : m == 1 ? wet : (1 - m) * dry + m * wet;
        play[i] = hold > 0 ? 1 : 0;
        if (hold > 0)
            hold--;
    }

    st[S_HOLD] = (float)hold;
    st[S_POS] = (float)f.pos;
    st[S_HOP] = (float)f.hop;

    return 0;
}
