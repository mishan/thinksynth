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

/* One pole over the input magnitude, the same coefficient in both
   directions. env::follower shipped with this same description and is the
   asymmetric one -- fast up, slow down. */
static const char desc[] = "Averages the magnitude of the input";
thPlugin::State    mystate = thPlugin::ACTIVE;

void module_cleanup (thPlugin *plugin)
{
}

enum { OUT_ARG,INOUT_LAST,IN_ARG,IN_FALLOFF };

int args[IN_FALLOFF + 1];

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The averaged magnitude");
    plugin->setArgRange(args[OUT_ARG], 0, TH_MAX);
    plugin->setArgUnits(args[OUT_ARG], "full scale");
    args[INOUT_LAST] = plugin->regArg("last", thPlugin::ARG_STATE);
    args[IN_ARG] = plugin->regArg("in", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_ARG], "Signal in; its magnitude is followed");
    plugin->setArgRange(args[IN_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[IN_ARG], "full scale");
    args[IN_FALLOFF] = plugin->regArg("falloff", thPlugin::ARG_IN);
    /* The coefficient is 0.1^falloff -- decades of smoothing, not a time.
       Unlike env::follower this interpolates towards the magnitude rather
       than adding to it, so a coefficient of 1 really does arrive at once. */
    plugin->setArgDesc(args[IN_FALLOFF],
                       "0 is instantaneous; each whole number is ten times "
                       "slower");

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out, *out_last;
    thArg *in_arg, *in_falloff;
    thArg *out_arg;
    thArg *inout_last;
    unsigned int i;
    float last, absolute, falloff;

    out_arg = mod->getArg(node, args[OUT_ARG]);
    inout_last = mod->getArg(node, args[INOUT_LAST]);

    last = (*inout_last)[0];

    out_last = inout_last->allocate(1);

    out = out_arg->allocate(windowlen);

    in_arg = mod->getArg(node, args[IN_ARG]);
    in_falloff = mod->getArg(node, args[IN_FALLOFF]);


    for(i = 0; i < windowlen; i++)
    {
        absolute = fabs((*in_arg)[i]);
        falloff = pow(0.1, (*in_falloff)[i]);

        last *= 1 - falloff;
        last += absolute * falloff;

        out[i] = last;
    }

    out_last[0] = last;

    

    return 0;
}
