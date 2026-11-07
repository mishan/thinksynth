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

#include "think.h"

#include "noiseslot.h"
#include "plugins/dice.h"

static const char desc[] = "Produces Random Signal";
thPlugin::State    mystate = thPlugin::ACTIVE;

/* The seed source: one generator per synth, claimed here and released in
 * module_cleanup. plugins/osc/noiseslot.h is the whole argument -- the same
 * one osc::noise seeds from, which is why it is a header and not a copy.
 *
 * Each node draws from its own stream, seeded from that one (see
 * osc::noise). Nothing seeds the synth's, which is why thcNodeHost still
 * refuses this plugin in a chain. */

void module_cleanup (thPlugin *plugin)
{
    thNoiseRelease(plugin);
}

void module_reset (thPlugin *plugin)
{
    thNoiseRestart(plugin);
}

/* ModuleLoad() invokes this function with a pointer to the plugin
 * instance. */
enum { OUT_ARG,INOUT_LAST,IN_SAMPLE };

std::atomic<int> args[IN_SAMPLE + 1];

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    thNoiseClaim(plugin);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "White noise, held for `sample'");
    plugin->setArgRange(args[OUT_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[OUT_ARG], "full scale");
    args[INOUT_LAST] = plugin->regArg("last", thPlugin::ARG_STATE);
    args[IN_SAMPLE] = plugin->regArg("sample", thPlugin::ARG_IN);
    /* `if (++position > sample)' -- how long a drawn value is held, so 0 is
       a new one every sample and a larger number is a coarser noise. */
    plugin->setArgDesc(args[IN_SAMPLE], "How long to hold each value");
    plugin->setArgUnits(args[IN_SAMPLE], "samples");
    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    int i;
    float *out;
    float *out_last;
    float position, last;
    thArg* in_sample;  /* How often to change the random number */
    thArg* out_arg;
    thArg* inout_last;  /* So sample can be consistant over windows */

    out_arg = mod->getArg(node, args[OUT_ARG]);
    inout_last = mod->getArg(node, args[INOUT_LAST]);
    position = (*inout_last)[0];
    last = (*inout_last)[1];

    float dice[2] = { (*inout_last)[2], (*inout_last)[3] };

    if ((*inout_last)[4] == 0)
        thDiceSeedBits(dice, thDiceMix(thNoiseSeed(thNoiseSlotFor(
            node->plugin()), mod->seedGeneration()), mod->seed()));

    /* `last' carries the position counter, the held sample and the node's
       stream across windows, all written back below. */
    out_last = inout_last->allocate(5);
    out = out_arg->allocate(windowlen);

    in_sample = mod->getArg(node, args[IN_SAMPLE]);

    for(i=0; i < (int)windowlen; i++) {
        if(++position > (*in_sample)[i]) {
            out[i] = TH_RANGE * thDiceNext(dice) + TH_MIN;
            position = 0;
            last = out[i];
        } else {
            out[i] = last;
        }
    }

    out_last[0] = position;
    out_last[1] = last;
    out_last[2] = dice[0];
    out_last[3] = dice[1];
    out_last[4] = 1;
/*    node->SetArg("out", out, windowlen);
    node->SetArg("last", last, 2);
*/
    return 0;
}
