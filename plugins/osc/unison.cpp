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

/* A stack of detuned saws in one node: the supersaw.
 *
 * Each voice is osc::blep's saw (plugins/osc/blep.h), so it aliases as
 * little as blep's and cannot drift from it. Voice 0 is at `freq'; voice k is
 * (k + 1) / 2 steps of `detune' cents from it, above for odd k and below
 * for even, the pitch worked out as supersaw.dsp's math nodes spell it --
 * `freq * exp2(detune * steps / 1200)' going up and divided by it going
 * down -- so the same seven saws land on the same frequencies.
 *
 * THE SIDES. `out_left' is the center and the voices above, `out_right'
 * the center and the voices below, each the mean of its own voices; `out'
 * is the mean of all of them, which is not the two sides' mean, since the
 * center is in both. A mean keeps the peaks at full scale, and the
 * loudness falls with the count: seven voices are some 8.5 dB under one,
 * and with an even count the left side has a voice more than the right
 * and is a little quieter.
 *
 * THE PHASES. Voice k starts at the fractional part of k times the golden
 * ratio, so no two voices begin on an edge together -- the click a stack
 * of saws started at 0 makes on every note -- and a render is the same
 * every time, with no draw from the synth's noise.
 *
 * `voices' is read on the note's first sample and held: a voice brought in
 * or taken out mid-note would be a step in each side's mean.
 *
 * A voice past half the rate is silent, and still counted in its side's
 * mean: held at Nyquist instead, several of a high note's voices would
 * stack there as one loud tone.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

#include "blep.h"

#include "thArg.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thSynthTree.h"
#include "thSynth.h"

#define UNISON_MAX 16

enum {IN_FREQ, IN_VOICES, IN_DETUNE, OUT_ARG, OUT_LEFT, OUT_RIGHT,
      INOUT_STATE};

std::atomic<int> args[INOUT_STATE + 1];

static const char desc[] = "Detuned band-limited saws, the center and a side "
                           "each above and below";
thPlugin::State    mystate = thPlugin::ACTIVE;

void module_cleanup (thPlugin *plugin)
{
}

void module_prepare (thNode *node, thSynthTree *mod, unsigned int samples,
                     int voice)
{
    if (voice)
        mod->getArg(node, args[INOUT_STATE])->reserve(1 + UNISON_MAX);
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[IN_FREQ] = plugin->regArg("freq", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_FREQ], "The center voice's frequency");
    plugin->setArgUnits(args[IN_FREQ], "Hz");
    args[IN_VOICES] = plugin->regArg("voices", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_VOICES],
                       "How many saws, read on the note's first sample");
    plugin->setArgRange(args[IN_VOICES], 1, UNISON_MAX);
    plugin->setArgDefault(args[IN_VOICES], 7);
    plugin->setArgStep(args[IN_VOICES], 1);
    args[IN_DETUNE] = plugin->regArg("detune", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_DETUNE], "The step between voices");
    plugin->setArgRange(args[IN_DETUNE], 0, 100);
    plugin->setArgUnits(args[IN_DETUNE], "cents");
    plugin->setArgDefault(args[IN_DETUNE], 12);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The mean of every voice");
    plugin->setArgRange(args[OUT_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[OUT_ARG], "full scale");
    args[OUT_LEFT] = plugin->regArg("out_left", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_LEFT],
                       "The mean of the center and the voices above it");
    plugin->setArgRange(args[OUT_LEFT], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[OUT_LEFT], "full scale");
    args[OUT_RIGHT] = plugin->regArg("out_right", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_RIGHT],
                       "The mean of the center and the voices below it");
    plugin->setArgRange(args[OUT_RIGHT], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[OUT_RIGHT], "full scale");

    /* [0] the note's voice count, 0 before its first sample; [1 + k]
       voice k's phase. */
    args[INOUT_STATE] = plugin->regArg("state", thPlugin::ARG_STATE);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    const thArgReader freq(mod->getArg(node, args[IN_FREQ]));
    const thArgReader detune(mod->getArg(node, args[IN_DETUNE]));
    thArg *in_voices = mod->getArg(node, args[IN_VOICES]);
    thArg *inout_state = mod->getArg(node, args[INOUT_STATE]);
    const float rate = (float)samples;
    thkBlep osc[UNISON_MAX];
    int n = (int)(*inout_state)[0];

    if (n < 1)
    {
        n = (int)thClampArg((*in_voices)[0], 1, UNISON_MAX);
        for (int k = 0; k < n; k++)
            osc[k].phase = (float)fmod(k * 0.6180339887498949, 1.0);
    }
    else
        for (int k = 0; k < n; k++)
            osc[k].phase = (*inout_state)[1 + k];
    for (int k = 0; k < n; k++)
        osc[k].started = true;

    float *state = inout_state->allocate(1 + UNISON_MAX);
    float *out = mod->getArg(node, args[OUT_ARG])->allocate(windowlen);
    float *left = mod->getArg(node, args[OUT_LEFT])->allocate(windowlen);
    float *right = mod->getArg(node, args[OUT_RIGHT])->allocate(windowlen);
    const int nLeft = 1 + n / 2, nRight = 1 + (n - 1) / 2;

    /* Each voice's cycle is worked out again only when its frequency
       changes, and the ratios only when the detune does, compared as
       bits: the same bits make the same cycle, a NaN's included. */
    float ratio[UNISON_MAX];
    thkBlep::Ctl c[UNISON_MAX];
    bool loud[UNISON_MAX];
    float heldFreq = 0, heldDetune = 0;
    bool fresh = true;

    for (unsigned int i = 0; i < windowlen; i++)
    {
        const float f = freq[i], d = thkClampMag(detune[i], 1200);
        const bool newDetune = fresh || memcmp(&d, &heldDetune, sizeof(d));

        if (newDetune)
        {
            heldDetune = d;
            for (int k = 1; k < n; k++)
                ratio[k] = exp2f(d * (float)((k + 1) / 2) / 1200);
        }
        if (newDetune || memcmp(&f, &heldFreq, sizeof(f)))
        {
            heldFreq = f;
            for (int k = 0; k < n; k++)
            {
                const float hz = k == 0 ? f : k % 2 ? f * ratio[k] : f / ratio[k];

                c[k] = thkBlep::ctl(hz, 0, 0.5f, rate);
                loud[k] = !(thkFinite(hz) && hz > rate * 0.5f);
            }
        }
        fresh = false;

        float sync, edge;
        const auto voice = [&](int k) {
            const float v = osc[k].tick(c[k], 0, 0, sync, edge);

            return loud[k] ? v : 0;
        };
        const float center = voice(0);
        float all = 0 + center, up = 0 + center, down = 0 + center;

        /* In pairs, one above and one below, which keeps every sum in
           ascending order. */
        for (int k = 1; k < n; k += 2)
        {
            const float above = voice(k);

            all += above;
            up += above;
            if (k + 1 < n)
            {
                const float below = voice(k + 1);

                all += below;
                down += below;
            }
        }

        out[i] = TH_MAX * (all / n);
        left[i] = TH_MAX * (up / nLeft);
        right[i] = TH_MAX * (down / nRight);
    }

    state[0] = (float)n;
    for (int k = 0; k < UNISON_MAX; k++)
        state[1 + k] = k < n ? osc[k].phase : 0;

    return 0;
}
