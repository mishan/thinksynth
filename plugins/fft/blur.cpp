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

/* Spectral blur: each frequency's level smeared over `blur', so that an
 * attack swells in and a sound that stops fades out where it was.
 *
 * On plugins/fft/stft.h's running transform, a frame late. Each bin's
 * level S follows the frame's magnitude M by a one-pole of time constant
 * `blur', and is played at the phase the bin should have:
 *
 *   - while M is near S or over it, the bin holds a sound, so its phase is
 *     the frame's own, and its turn from the last frame to this one is
 *     learned as the bin's rotation, z = X conj(X') / |X X'|;
 *   - once M falls well under S the sound has stopped or changed, and the
 *     frame's own phase there is leakage that the overlapping frames
 *     would cancel. The bin keeps turning, as fft::freeze's held bins do,
 *     by the rotation of the strongest bin within two of it -- a partial
 *     spans several bins, and the ones at its edges learn their turns
 *     from the very frames in which it stops; locked to its peak, the
 *     whole partial fades as one -- and by `random' of a hashed jitter,
 *     which towards 1 turns a tail into a shimmer.
 *
 * Measured on a sine that stops under a half-second blur, its tail falls
 * at the blur's time constant from 2 dB under the sine, and steady noise
 * comes out 0.7 dB under its level at any `random'.
 *
 * At `blur' 0 the level is the magnitude, every bin holds its sound, and
 * the output is the input a frame late, to float error.
 *
 * `mix' blends the input a frame late against the blurred: the dry signal
 * delayed to match, so the two do not comb. Inside an instrument the last
 * frame of each note, and all of its blurred tail, is lost when the voice
 * ends; an instrument that wants the tail holds its voice past the note by
 * the frame and the blur.
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

enum {IN_ARG, IN_SIZE, IN_BLUR, IN_RANDOM, IN_MIX, IN_SEED, OUT_ARG, INOUT_STATE};

std::atomic<int> args[INOUT_STATE + 1];

static const char desc[] = "Spectral blur: each frequency's level smeared over time";
thPlugin::State    mystate = thPlugin::ACTIVE;

#define MAXN THK_FFT_MAX
#define BINS (MAXN / 2 + 1)

/* The longest blur, 95 s at 44.1k: longer is a level that never moves
   enough to hear, and a one-pole's step that would, far past this, round
   to nothing in float. */
#define BLURMAX 4194304.0f

/* A bin is falling, and turns on by its rotation rather than taking the
   frame's phase, once its magnitude is under this share of its level.
   Not under the level itself: steady noise dips under its own level half
   the time, and every dip given a borrowed phase cost it 3 dB. */
#define FALL 0.25f

enum {
    S_N, S_POS, S_HOP, S_FRAME,
    S_SCALARS = 8,
    S_RING = S_SCALARS,
    S_ACC = S_RING + MAXN,
    S_LEVEL = S_ACC + MAXN,
    S_XR = S_LEVEL + BINS, S_XI = S_XR + BINS,      /* the last frame */
    S_ZR = S_XI + BINS, S_ZI = S_ZR + BINS,         /* each bin's rotation */
    S_PR = S_ZI + BINS, S_PI = S_PR + BINS,         /* each bin's phase */
    S_LEN = S_PI + BINS
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

    args[IN_BLUR] = plugin->regArg("blur", thPlugin::ARG_IN);
    plugin->setArgSmooth(args[IN_BLUR], TH_SMOOTH_MS);
    plugin->setArgDesc(args[IN_BLUR],
                       "How long a frequency's level takes to follow; 0 is none");
    plugin->setArgUnits(args[IN_BLUR], "samples");

    args[IN_RANDOM] = plugin->regArg("random", thPlugin::ARG_IN);
    plugin->setArgSmooth(args[IN_RANDOM], TH_SMOOTH_MS);
    plugin->setArgDesc(args[IN_RANDOM],
                       "How a fading frequency turns: 0 as it was, 1 at random");
    plugin->setArgRange(args[IN_RANDOM], 0, 1);

    args[IN_MIX] = plugin->regArg("mix", thPlugin::ARG_IN);
    plugin->setArgSmooth(args[IN_MIX], TH_SMOOTH_MS);
    plugin->setArgDesc(args[IN_MIX],
                       "0 the input a frame late, 1 the blurred");
    plugin->setArgRange(args[IN_MIX], 0, 1);

    args[IN_SEED] = plugin->regArg("seed", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_SEED], "Which random turns");

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The blend, a frame late");
    plugin->setArgRange(args[OUT_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[OUT_ARG], "full scale");

    args[INOUT_STATE] = plugin->regArg("state", thPlugin::ARG_STATE);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    const thArgReader in(mod->getArg(node, args[IN_ARG])),
                      size(mod->getArg(node, args[IN_SIZE])),
                      blur(mod->getArg(node, args[IN_BLUR])),
                      random(mod->getArg(node, args[IN_RANDOM])),
                      mix(mod->getArg(node, args[IN_MIX])),
                      seedArg(mod->getArg(node, args[IN_SEED]));
    float *st = mod->getArg(node, args[INOUT_STATE])->allocate(S_LEN);
    float *out = mod->getArg(node, args[OUT_ARG])->allocate(windowlen);

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
              st[S_POS] >= sn || st[S_HOP] < 0 || st[S_HOP] >= sn / 4;
    }
    if (bad)
        memset(st, 0, S_LEN * sizeof(float));
    if (st[S_N] == 0)
        st[S_N] = (float)thkStftSize(size[0]);

    thkStft f = { st + S_RING, st + S_ACC, (int)st[S_N], (int)st[S_POS], (int)st[S_HOP] };
    float *level = st + S_LEVEL, *xr = st + S_XR, *xi = st + S_XI;
    float *zr = st + S_ZR, *zi = st + S_ZI, *pr = st + S_PR, *pi = st + S_PI;
    const int h = f.n / 4;

    for (unsigned int i = 0; i < windowlen; i++)
    {
        const float b = thkClamp(blur[i], 0, BLURMAX), r = thkClamp(random[i], 0, 1);
        const float m = thkClamp(mix[i], 0, 1);
        float seedf = thkFinite(seedArg[i]) ? seedArg[i] : 0;
        uint32_t seed;

        if (seedf == 0)
            seedf = 0;      /* -0 too */
        memcpy(&seed, &seedf, sizeof(seed));

        /* Bounded, so that no finite input can carry a frame's magnitudes
           past a float. */
        const float v = thkClampMag(in[i], 1e6f);
        float dry;
        const float wet = f.tick(v, dry, [&](float *re, float *im, int n) {
            /* The level is a power's one-pole -- its square follows the
               magnitude's square -- so steady noise keeps its RMS rather
               than the mean magnitude's 1 dB less; at half the time
               constant, so the level itself falls at `blur'. */
            const float a = b < 1 ? 1 : 1 - expf(-2 * (float)h / b);
            const uint32_t frame = (uint32_t)st[S_FRAME];
            float mag[BINS];

            for (int k = 0; k <= n / 2; k++)
            {
                const float m2 = re[k] * re[k] + im[k] * im[k], l2 = level[k] * level[k];
                const float s = a >= 1 ? sqrtf(m2) : sqrtf(l2 + a * (m2 - l2));

                mag[k] = sqrtf(m2);
                level[k] = s < 1e-30f ? 0 : s;
            }
            for (int k = 0; k <= n / 2; k++)
            {
                const float mk = mag[k], s = level[k];
                const float cr = re[k] * xr[k] + im[k] * xi[k];
                const float ci = im[k] * xr[k] - re[k] * xi[k];
                const float d = sqrtf(cr * cr + ci * ci);

                if (mk >= FALL * s)
                {
                    /* Holding: the frame's own phase, and the turn since
                       the last frame learned. */
                    if (d > 0)
                    {
                        zr[k] = cr / d;
                        zi[k] = ci / d;
                    }
                    else if (zr[k] == 0 && zi[k] == 0)
                        zr[k] = 1;      /* a fresh state's turn is none */
                    pr[k] = mk > 0 ? re[k] / mk : 1;
                    pi[k] = mk > 0 ? im[k] / mk : 0;
                }
                else
                {
                    /* Falling: turned by the rotation of the strongest bin
                       within two of it, so a partial's lobe turns as one,
                       and by `random' of a jitter. */
                    int peak = k;

                    for (int o = -2; o <= 2; o++)
                        if (k + o >= 0 && k + o <= n / 2 && level[k + o] > level[peak])
                            peak = k + o;

                    float c = pr[k] * zr[peak] - pi[k] * zi[peak];
                    float e = pr[k] * zi[peak] + pi[k] * zr[peak];

                    if (r > 0)
                    {
                        const float t = r * thkStftJitter(seed, frame, (uint32_t)k);
                        const float jc = thkSin2pi(t + 0.25f), js = thkSin2pi(t);
                        const float c2 = c * jc - e * js;

                        e = c * js + e * jc;
                        c = c2;
                    }

                    const float len = sqrtf(c * c + e * e);

                    pr[k] = len > 0 ? c / len : 1;
                    pi[k] = len > 0 ? e / len : 0;
                }
            }
            for (int k = 0; k <= n / 2; k++)
            {
                xr[k] = re[k];
                xi[k] = im[k];
                re[k] = level[k] * pr[k];
                im[k] = level[k] * pi[k];
            }
            st[S_FRAME] = (float)((frame + 1) & 0xffffff);
        });

        out[i] = m == 0 ? dry : m == 1 ? wet : (1 - m) * dry + m * wet;
    }

    st[S_POS] = (float)f.pos;
    st[S_HOP] = (float)f.hop;

    return 0;
}
