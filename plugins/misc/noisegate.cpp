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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

static const char desc[] = "Zeros the output if the input goes below a certain level";
thPlugin::State    mystate = thPlugin::ACTIVE;

void module_cleanup (thPlugin *plugin)
{
}

enum { OUT_ARG,INOUT_LAST,IN_ARG,IN_FALLOFF,IN_CUTOFF };

int args[IN_CUTOFF + 1];

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    /* The input passed through or replaced by silence -- it does not fade,
       so an audible signal at the threshold will chatter. */
    plugin->setArgDesc(args[OUT_ARG], "The input, or nothing");
    plugin->setArgRange(args[OUT_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[OUT_ARG], "full scale");
    args[INOUT_LAST] = plugin->regArg("last", thPlugin::ARG_STATE);
    args[IN_ARG] = plugin->regArg("in", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_ARG], "Signal in");
    plugin->setArgRange(args[IN_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[IN_ARG], "full scale");
    args[IN_FALLOFF] = plugin->regArg("falloff", thPlugin::ARG_IN);
    /* env::followavg's averager, coefficient and all, deciding what counts as
       the current level. 0.1^falloff, so decades of smoothing rather than a
       time. */
    plugin->setArgDesc(args[IN_FALLOFF],
                       "How the level is averaged: 0 is instantaneous, and "
                       "each whole number is ten times slower");
    args[IN_CUTOFF] = plugin->regArg("cutoff", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_CUTOFF],
                       "The averaged level below which nothing gets through");
    plugin->setArgRange(args[IN_CUTOFF], 0, TH_MAX);
    plugin->setArgUnits(args[IN_CUTOFF], "full scale");
    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out, *out_last;
    thArg *in_arg, *in_falloff, *in_cutoff;
    thArg *out_arg;
    thArg *inout_last;
    unsigned int i;
    float last, in, absolute, falloff;

    out_arg = mod->getArg(node, args[OUT_ARG]);
    inout_last = mod->getArg(node, args[INOUT_LAST]);

    last = (*inout_last)[0];

    out_last = inout_last->allocate(1);

    out = out_arg->allocate(windowlen);

    in_arg = mod->getArg(node, args[IN_ARG]);
    in_falloff = mod->getArg(node, args[IN_FALLOFF]);
    in_cutoff = mod->getArg(node, args[IN_CUTOFF]);

    for(i = 0; i < windowlen; i++)
    {
        in = (*in_arg)[i];
        absolute = fabs(in);
        falloff = pow(0.1, (*in_falloff)[i]);

        last *= 1 - falloff;
        last += absolute * falloff;

        if(last >= (*in_cutoff)[i])
        {
            out[i] = in;
        }
        else
        {
            out[i] = 0;
        }
    }

    out_last[0] = last;

    

    return 0;
}
