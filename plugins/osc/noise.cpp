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
 * Determinism: each node keeps its own generator (plugins/dice.h) in its
 * state, seeded on its first window by one draw from the synth's stream
 * (plugins/osc/noiseslot.h), which starts where every synth's starts. Two
 * fresh synths render the same noise, which is what dspcheck's render-twice
 * comparison and the wasm-against-native gate both rest on, and a graph with
 * two noise nodes renders the same at any window length.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

#include "noiseslot.h"
#include "plugins/dice.h"

#include "noise.h"

static const char desc[] = "White, pink or brown noise";
thPlugin::State    mystate = thPlugin::ACTIVE;

static const char *const colors[] = { "White", "Pink", "Brown" };

enum {OUT_ARG, IN_COLOR, IN_AMP, INOUT_LAST};
std::atomic<int> args[INOUT_LAST + 1];

void module_cleanup (thPlugin *plugin)
{
    thNoiseRelease(plugin);
}

void module_reset (thPlugin *plugin)
{
    thNoiseRestart(plugin);
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

    /* The two filters' history -- three pink poles and the brown
       integrator -- then the node's own stream and whether it is seeded.
       Per node, so two noise nodes do not share a filter or a stream, and
       across windows, so neither restarts every 1024 samples. */
    args[INOUT_LAST] = plugin->regArg("last", thPlugin::ARG_STATE);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    thArg *inout_last = mod->getArg(node, args[INOUT_LAST]);
    thArg *in_color = mod->getArg(node, args[IN_COLOR]);
    thArg *in_amp = mod->getArg(node, args[IN_AMP]);
    thkNoise noise;

    /* [0..3] the pink filter's three poles and the brown, [4] and [5] the
       generator's high and low 24 bits, [6] whether it is seeded. */
    noise.b0 = (*inout_last)[0];
    noise.b1 = (*inout_last)[1];
    noise.b2 = (*inout_last)[2];
    noise.brown = (*inout_last)[3];
    noise.x = ((uint64_t)(uint32_t)(*inout_last)[4] << 24) |
              (uint64_t)(uint32_t)(*inout_last)[5];
    if ((*inout_last)[6] == 0)
        noise.seed(thDiceMix(thNoiseSeed(thNoiseSlotFor(node->plugin()),
                                         mod->seedGeneration()), mod->seed()));

    /* Both filters feed back, so one non-finite sample would be read back
       for the life of the note. Nothing here can produce one -- the input is
       a bounded draw and every pole is inside the unit circle -- so this is
       about a buffer that has been somewhere else. */
    if (!thIsFinite(noise.b0) || !thIsFinite(noise.b1) || !thIsFinite(noise.b2) ||
        !thIsFinite(noise.brown))
        noise.b0 = noise.b1 = noise.b2 = noise.brown = 0;

    float *last = inout_last->allocate(7);
    float *out = mod->getArg(node, args[OUT_ARG])->allocate(windowlen);
    const bool still = in_color->len() <= 1 && in_amp->len() <= 1;
    thkNoise::Ctl c = thkNoise::ctl((*in_color)[0], (*in_amp)[0] / TH_MAX);

    for (unsigned int i = 0; i < windowlen; i++)
    {
        if (!still)
            c = thkNoise::ctl((*in_color)[i], (*in_amp)[i] / TH_MAX);
        out[i] = TH_MAX * noise.tick(c);
    }

    last[0] = noise.b0;
    last[1] = noise.b1;
    last[2] = noise.b2;
    last[3] = noise.brown;
    last[4] = (float)(uint32_t)(noise.x >> 24);
    last[5] = (float)(uint32_t)(noise.x & 0xFFFFFFu);
    last[6] = 1;

    return 0;
}
