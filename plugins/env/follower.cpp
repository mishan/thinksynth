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

/* A peak follower: it rises by a fraction of the input and falls by a
   fraction of itself, so it climbs fast and lets go slowly. env::followavg
   shipped with this same description and is the symmetric one. */
static const char desc[] = "Follows the peaks of the input";
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
    plugin->setArgDesc(args[OUT_ARG], "The followed magnitude");
    plugin->setArgRange(args[OUT_ARG], 0, TH_MAX);
    plugin->setArgUnits(args[OUT_ARG], "full scale");
    args[INOUT_LAST] = plugin->regArg("last", thPlugin::ARG_STATE);
    args[IN_ARG] = plugin->regArg("in", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_ARG], "Signal in; its magnitude is followed");
    plugin->setArgRange(args[IN_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[IN_ARG], "full scale");
    args[IN_FALLOFF] = plugin->regArg("falloff", thPlugin::ARG_IN);
    /* The coefficient is 0.1^falloff, so this is decades of smoothing rather
       than a time: 3 gives 0.001 and crawls. No range -- larger is only ever
       slower. Small is not symmetrical with large here: the rise adds
       `magnitude * coefficient' to what is already there rather than
       interpolating towards it, so at 0 the coefficient is 1 and a rise
       *doubles* on a held signal instead of tracking it. Saying "0 is
       instantaneous" would describe env::followavg, which does interpolate.
       Negative is worse again -- the coefficient passes 1 and the state
       rings. */
    plugin->setArgDesc(args[IN_FALLOFF],
                       "Each whole number is ten times slower; under about 1 "
                       "a rise overshoots rather than tracking");
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

        if(absolute >= last)
        {
            last += absolute * falloff;
        }
        else
        {
            last -= last * falloff;
        }

        out[i] = last;
    }

    out_last[0] = last;

    

    return 0;
}
