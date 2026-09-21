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

#include "think.h"

enum {IN_INMIN, IN_INMAX, IN_OUTMIN, IN_OUTMAX, IN_ARG, OUT_ARG};
int args[OUT_ARG + 1];

/* "(dynamic)" as against env::map, whose four bounds were once read only at
   sample 0. They are not any more -- map fetches its args with getBuffer(),
   which repeats an arg over the window exactly as operator[] does here -- so
   the two now compute the same thing by two routes. */
static const char desc[] = "Maps a stream to a new value range (dynamic)";
thPlugin::State    mystate = thPlugin::PASSIVE;

void module_cleanup (thPlugin *plugin)
{
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    /* out = (in - inmin)/(inmax - inmin) * (outmax - outmin) + outmin. It
       does not clamp, so an input outside inmin..inmax is extrapolated rather
       than held -- which is how scripts/guard/divergent.dsp gets a
       cutoff a hundred times full scale out of a range declared 30 to 90,
       and why it diverges. And inmax == inmin divides by zero. */
    args[IN_INMIN] = plugin->regArg("inmin", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_INMIN], "Bottom of the range coming in");
    args[IN_INMAX] = plugin->regArg("inmax", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_INMAX],
                       "Top of the range coming in; must differ from inmin");
    args[IN_OUTMIN] = plugin->regArg("outmin", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_OUTMIN], "What inmin comes out as");
    args[IN_OUTMAX] = plugin->regArg("outmax", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_OUTMAX], "What inmax comes out as");
    args[IN_ARG] = plugin->regArg("in", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_ARG], "Signal in");
    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG],
                       "The input on the new scale, not clamped to it");

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out;
    thArg *in_arg, *in_min, *in_max, *out_min, *out_max;
    thArg *out_arg;
    unsigned int i;
    float percent;
    float val_imin, val_imax, val_omin, val_omax, val_arg;

    out_arg = mod->getArg(node, args[OUT_ARG]);
    out = out_arg->allocate(windowlen);

    in_arg = mod->getArg(node, args[IN_ARG]);
    in_min = mod->getArg(node, args[IN_INMIN]);
    in_max = mod->getArg(node, args[IN_INMAX]);
    out_min = mod->getArg(node, args[IN_OUTMIN]);
    out_max = mod->getArg(node, args[IN_OUTMAX]);

    for(i = 0; i < windowlen; i++) {
        val_arg = (*in_arg)[i];
        val_imin = (*in_min)[i];
        val_imax = (*in_max)[i];
        val_omin = (*out_min)[i];
        val_omax = (*out_max)[i];

        percent = (val_arg-val_imin)/(val_imax-val_imin);
        out[i] = (percent*(val_omax-val_omin))+val_omin;
    }

/*    node->SetArg("out", out, windowlen); */
    return 0;
}

