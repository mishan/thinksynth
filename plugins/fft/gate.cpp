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

/* Spectral gate: each frequency passed or taken away by how loud it is
 * against the loudest, so a sound thins to its strongest partials, or,
 * inverted, loses them and keeps what is under them.
 *
 * On plugins/fft/stft.h's running transform, a frame late. Each frame a
 * bin passes when its magnitude is within `thresh' dB of the frame's
 * loudest bin. Relative, so one setting thins a sound alike at any level.
 *
 * A bin's gain moves toward open or shut by a one-pole of `smooth'
 * samples, rising and falling alike: a hard gate (smooth 0) opens and
 * shuts bins frame by frame on noise near the threshold, the chirping a
 * spectral gate is known for, and smoothing them is what keeps it out.
 * The gains start open, and a silent frame opens them, so a sound is not
 * gated in from silence.
 *
 * `mix' blends the input a frame late against the gated: the dry signal
 * delayed to match, so the two do not comb.
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

enum {IN_ARG, IN_SIZE, IN_THRESH, IN_INVERT, IN_SMOOTH, IN_MIX, OUT_ARG, INOUT_STATE};

std::atomic<int> args[INOUT_STATE + 1];

static const char desc[] = "Spectral gate: each frequency passed by its level against the loudest";
thPlugin::State    mystate = thPlugin::ACTIVE;

static const char *const offOn[] = { "Off", "On" };

#define MAXN THK_FFT_MAX
#define BINS (MAXN / 2 + 1)

/* As fft::blur's longest blur. */
#define SMOOTHMAX 4194304.0f

enum {
    S_N, S_POS, S_HOP,
    S_SCALARS = 8,
    S_RING = S_SCALARS,
    S_ACC = S_RING + MAXN,
    S_SHUT = S_ACC + MAXN,      /* each bin's 1 - gain, so a fresh state is open */
    S_LEN = S_SHUT + BINS
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

    args[IN_THRESH] = plugin->regArg("thresh", thPlugin::ARG_IN);
    plugin->setArgSmooth(args[IN_THRESH], TH_SMOOTH_MS);
    plugin->setArgDesc(args[IN_THRESH],
                       "How far under the loudest frequency one still passes; 0 is -30");
    plugin->setArgRange(args[IN_THRESH], -90, 0);
    plugin->setArgDefault(args[IN_THRESH], -30);
    plugin->setArgUnits(args[IN_THRESH], "dB");

    args[IN_INVERT] = plugin->regArg("invert", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_INVERT], "On passes what the gate would take away");
    plugin->setArgValues(args[IN_INVERT], offOn, 2);

    args[IN_SMOOTH] = plugin->regArg("smooth", thPlugin::ARG_IN);
    plugin->setArgSmooth(args[IN_SMOOTH], TH_SMOOTH_MS);
    plugin->setArgDesc(args[IN_SMOOTH],
                       "How long a frequency takes to open or shut; 0 at once");
    plugin->setArgUnits(args[IN_SMOOTH], "samples");

    args[IN_MIX] = plugin->regArg("mix", thPlugin::ARG_IN);
    plugin->setArgSmooth(args[IN_MIX], TH_SMOOTH_MS);
    plugin->setArgDesc(args[IN_MIX], "0 the input a frame late, 1 the gated");
    plugin->setArgRange(args[IN_MIX], 0, 1);

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
                      thresh(mod->getArg(node, args[IN_THRESH])),
                      invert(mod->getArg(node, args[IN_INVERT])),
                      smooth(mod->getArg(node, args[IN_SMOOTH])),
                      mix(mod->getArg(node, args[IN_MIX]));
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
    float *shut = st + S_SHUT;
    const int h = f.n / 4;

    for (unsigned int i = 0; i < windowlen; i++)
    {
        const float t = !thkFinite(thresh[i]) || thresh[i] == 0 ? -30 : thkClamp(thresh[i], -90, 0);
        const bool inv = invert[i] > 0.5f;
        const float sm = thkClamp(smooth[i], 0, SMOOTHMAX), m = thkClamp(mix[i], 0, 1);

        /* Bounded, so that no finite input can carry a frame's magnitudes
           past a float. */
        const float v = thkClampMag(in[i], 1e6f);
        float dry;
        const float wet = f.tick(v, dry, [&](float *re, float *im, int n) {
            const float a = sm < 1 ? 1 : 1 - expf(-(float)h / sm);
            float loud = 0;

            for (int k = 0; k <= n / 2; k++)
                loud = fmaxf(loud, re[k] * re[k] + im[k] * im[k]);

            /* Compared squared: the threshold's power, against each bin's. */
            const float under = loud * expf(t * (0.1f * 2.30258509f));

            for (int k = 0; k <= n / 2; k++)
            {
                const float p = re[k] * re[k] + im[k] * im[k];
                const float target = loud == 0 || (p >= under) != inv ? 0.0f : 1.0f;
                float s = shut[k] + a * (target - shut[k]);

                if (s < 1e-30f)
                    s = 0;
                shut[k] = s;
                re[k] *= 1 - s;
                im[k] *= 1 - s;
            }
        });

        out[i] = m == 0 ? dry : m == 1 ? wet : (1 - m) * dry + m * wet;
    }

    st[S_POS] = (float)f.pos;
    st[S_HOP] = (float)f.hop;

    return 0;
}
