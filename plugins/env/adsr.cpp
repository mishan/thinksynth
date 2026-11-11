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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

#include "adsr.h"

enum {IN_A, IN_D, IN_S, IN_R, IN_P, IN_TRIGGER, IN_RESET, OUT_ARG, OUT_PLAY, INOUT_POSITION };
std::atomic<int> args[INOUT_POSITION + 1];

static const char desc[] = "ADSR Envelope Generator";
thPlugin::State    mystate = thPlugin::ACTIVE;


void module_cleanup (thPlugin *plugin)
{
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    /* Times are sample counts. A .dsp writes `200 ms' and thUnits folds it at
       the rate the synth is running at, so what arrives here is samples;
       zero-length segments complete at once rather than dividing. */
    args[IN_A] = plugin->regArg("a", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_A], "Attack; 0 jumps straight to the peak");
    plugin->setArgUnits(args[IN_A], "samples");
    args[IN_D] = plugin->regArg("d", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_D], "Decay, from the peak down to s");
    plugin->setArgUnits(args[IN_D], "samples");
    args[IN_S] = plugin->regArg("s", thPlugin::ARG_IN);
    /* A level, not a time -- the one arg of the four that is not. */
    plugin->setArgDesc(args[IN_S], "Sustain level; 0 ends the note");
    plugin->setArgRange(args[IN_S], 0, TH_MAX);
    plugin->setArgUnits(args[IN_S], "full scale");
    args[IN_R] = plugin->regArg("r", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_R], "Release; 0 ends the note at once");
    plugin->setArgUnits(args[IN_R], "samples");
    args[IN_P] = plugin->regArg("p", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_P], "Peak level");
    plugin->setArgRange(args[IN_P], 0, TH_MAX);
    plugin->setArgUnits(args[IN_P], "full scale");
    /* `if ((*in_p)[i] == 0) peak = TH_MAX' -- the zero case the plugin
       already had, written where nothing could read it. */
    plugin->setArgDefault(args[IN_P], TH_MAX);
    args[IN_TRIGGER] = plugin->regArg("trigger", thPlugin::ARG_IN);
    /* Read as `> 0' and `<= 0'. thMidiChan writes 2 for a note released but
       still held by the sustain pedal, which is why the range is not 0 to 1. */
    plugin->setArgDesc(args[IN_TRIGGER],
                       "Note Trigger: 0 released, 1 held, 2 held by the pedal");
    plugin->setArgRange(args[IN_TRIGGER], 0, 2);
    args[IN_RESET] = plugin->regArg("reset", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_RESET], "Reset to A phase");
    plugin->setArgRange(args[IN_RESET], 0, 1);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The envelope");
    plugin->setArgRange(args[OUT_ARG], 0, TH_MAX);
    plugin->setArgUnits(args[OUT_ARG], "full scale");
    args[OUT_PLAY] = plugin->regArg("play", thPlugin::ARG_OUT);
    /* What thMidiChan retires a voice on: the io node's `play' is wired to
       one of these, and a window ending at 0 is a note that is over. */
    plugin->setArgDesc(args[OUT_PLAY], "1 while the note is sounding");
    plugin->setArgRange(args[OUT_PLAY], 0, 1);

    args[INOUT_POSITION] = plugin->regArg("position", thPlugin::ARG_STATE);

    return 0;
}

/* A window of either kernel, its state carried in `position': the
   free-running envelope's [0] position and [1] stage; the gated one's
   [0] position, [1] stage, [2] whether the trigger was up, [3] the level
   and [4] where the attack or release started. The position's remainder
   (thSplitCount) is last: [2] free-running, [5] gated. */
template <class K>
static void run (K &env, thNode *node, thSynthTree *mod, unsigned int windowlen)
{
    thArg *in_a = mod->getArg(node, args[IN_A]);
    thArg *in_d = mod->getArg(node, args[IN_D]);
    thArg *in_s = mod->getArg(node, args[IN_S]);
    thArg *in_r = mod->getArg(node, args[IN_R]);
    thArg *in_p = mod->getArg(node, args[IN_P]);
    thArg *in_trigger = mod->getArg(node, args[IN_TRIGGER]);
    thArg *in_reset = mod->getArg(node, args[IN_RESET]);
    float *out = mod->getArg(node, args[OUT_ARG])->allocate(windowlen);
    float *play = mod->getArg(node, args[OUT_PLAY])->allocate(windowlen);

    /* The times and levels once a window while they hold still. */
    const bool still = in_a->len() <= 1 && in_d->len() <= 1 && in_s->len() <= 1 &&
                       in_r->len() <= 1 && in_p->len() <= 1;
    thkAdsrCtl c = thkAdsr((*in_a)[0], (*in_d)[0], (*in_s)[0] / TH_MAX,
                           (*in_r)[0], (*in_p)[0] / TH_MAX);

    /* A trigger and reset held through the window too: a phase at a time. */
    if (still && in_trigger->len() <= 1 && in_reset->len() <= 1)
    {
        env.run(c, (*in_trigger)[0], (*in_reset)[0], (int)windowlen, out, play);
        for (unsigned int i = 0; i < windowlen; i++)
            out[i] *= TH_MAX;
        return;
    }
    for (unsigned int i = 0; i < windowlen; i++)
    {
        if (!still)
            c = thkAdsr((*in_a)[i], (*in_d)[i], (*in_s)[i] / TH_MAX, (*in_r)[i],
                        (*in_p)[i] / TH_MAX);
        out[i] = TH_MAX * env.tick(c, (*in_trigger)[i], (*in_reset)[i], play[i]);
    }
}

/* The envelope as it always ran, for a trigger written as a constant -- or
   not written at all. Such a graph wants a one-shot: attack, decay, and at
   a trigger of 0 straight into the release, which is what brass.dsp's bend
   and bd10.dsp's pitch sweep are built on. */
static int freeRunning (thNode *node, thSynthTree *mod, unsigned int windowlen)
{
    thArg *inout = mod->getArg(node, args[INOUT_POSITION]);
    thkAdsrFree env;

    env.position = (uint32_t)((double)(*inout)[0] +
                              (inout->len() >= 3 ? (*inout)[2] : 0));
    env.phase = (int)(*inout)[1];

    float *state = inout->allocate(3);

    run(env, node, mod, windowlen);
    thSplitCount(env.position, state[0], state[2]);
    state[1] = env.phase;
    return 0;
}

static int gated (thNode *node, thSynthTree *mod, unsigned int windowlen)
{
    thArg *inout = mod->getArg(node, args[INOUT_POSITION]);
    thkAdsrGated env;

    /* A new node's state reads zeros, which is attack at position 0 -- so
       a fresh one starts out waiting instead. */
    if (inout->len() >= 6)
    {
        env.position = (uint32_t)((double)(*inout)[0] + (*inout)[5]);
        env.phase = (int)(*inout)[1];
        env.was = (*inout)[2] > 0;
        env.level = (*inout)[3];
        env.from = (*inout)[4];
    }

    float *state = inout->allocate(6);

    run(env, node, mod, windowlen);
    thSplitCount(env.position, state[0], state[5]);
    state[1] = env.phase;
    state[2] = env.was ? 1 : 0;
    state[3] = env.level;
    state[4] = env.from;
    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    /* The arg as the graph wrote it, before it is followed: a number, or
       nothing, is a constant; anything else moves. */
    const thArg *trigger = node->getArg((int)args[IN_TRIGGER]);

    if (trigger == NULL || trigger->type() == thArg::ARG_VALUE)
        return freeRunning(node, mod, windowlen);

    return gated(node, mod, windowlen);
}
