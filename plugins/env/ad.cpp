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

#include "ad.h"

/* An AD, not an ADSR: no sustain, no trigger, and the note is over when the
   decay is. env::adsr shipped with this same description. */
static const char desc[] = "AD Envelope Generator";
thPlugin::State    mystate = thPlugin::ACTIVE;

void module_cleanup (thPlugin *plugin)
{
}

enum { OUT_OUT,OUT_PLAY,INOUT_POSITION,IN_A,IN_D,IN_P,IN_RESET };

std::atomic<int> args[IN_RESET + 1];

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[OUT_OUT] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_OUT], "The envelope");
    plugin->setArgRange(args[OUT_OUT], 0, TH_MAX);
    plugin->setArgUnits(args[OUT_OUT], "full scale");
    args[OUT_PLAY] = plugin->regArg("play", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_PLAY], "1 while the note is sounding");
    plugin->setArgRange(args[OUT_PLAY], 0, 1);
    args[INOUT_POSITION] = plugin->regArg("position", thPlugin::ARG_STATE);
    /* No sustain and no trigger: it runs once from `reset' and is over.
       Times are sample counts -- see env::adsr. Both halves are half a sine
       rather than the logarithm env::adsr uses. */
    args[IN_A] = plugin->regArg("a", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_A], "Attack; 0 jumps straight to the peak");
    plugin->setArgUnits(args[IN_A], "samples");
    args[IN_D] = plugin->regArg("d", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_D], "Decay; when it ends, so does the note");
    plugin->setArgUnits(args[IN_D], "samples");
    args[IN_P] = plugin->regArg("p", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_P], "Peak level");
    plugin->setArgRange(args[IN_P], 0, TH_MAX);
    plugin->setArgUnits(args[IN_P], "full scale");
    /* `if ((*in_p)[i] == 0) peak = TH_MAX' -- the plugin's own zero case. */
    plugin->setArgDefault(args[IN_P], TH_MAX);
    args[IN_RESET] = plugin->regArg("reset", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_RESET], "Start again from the attack");
    plugin->setArgRange(args[IN_RESET], 0, 1);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    thArg *in_a = mod->getArg(node, args[IN_A]);
    thArg *in_d = mod->getArg(node, args[IN_D]);
    thArg *in_p = mod->getArg(node, args[IN_P]);
    thArg *in_reset = mod->getArg(node, args[IN_RESET]);
    /* [0] the position in the stage, [1] the stage, [2] the position's
       remainder (thSplitCount). */
    thArg *inout_position = mod->getArg(node, args[INOUT_POSITION]);
    thkAd env;

    env.position = (uint32_t)((double)(*inout_position)[0] +
                              (inout_position->len() >= 3 ? (*inout_position)[2] : 0));
    env.phase = (int)(*inout_position)[1];

    float *state = inout_position->allocate(3);
    float *out = mod->getArg(node, args[OUT_OUT])->allocate(windowlen);
    float *play = mod->getArg(node, args[OUT_PLAY])->allocate(windowlen);

    /* The times once a window while they hold still. */
    const bool still = in_a->len() <= 1 && in_d->len() <= 1 && in_p->len() <= 1;
    thkAd::Ctl c = thkAd::ctl((*in_a)[0], (*in_d)[0], (*in_p)[0] / TH_MAX);

    for (unsigned int i = 0; i < windowlen; i++)
    {
        if (!still)
            c = thkAd::ctl((*in_a)[i], (*in_d)[i], (*in_p)[i] / TH_MAX);
        out[i] = TH_MAX * env.tick(c, (*in_reset)[i], play[i]);
    }

    thSplitCount(env.position, state[0], state[2]);
    state[1] = env.phase;

    return 0;
}
