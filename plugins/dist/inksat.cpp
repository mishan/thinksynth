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

static const char desc[] = "Applies x^(1/y) saturation";

/* The exponent is 1/(factor + 1), so a factor of -1 asks for a division by
   zero and anything below it asks for a negative exponent -- and pow(0, -n)
   is an infinity, which a silent sample reaches on its own. The floor leaves
   the exponent at a thousand, which on an input inside full scale is already
   indistinguishable from silence. */
#define FACTOR_MIN (-0.999f)
thPlugin::State    mystate = thPlugin::PASSIVE;

void module_cleanup (thPlugin *plugin)
{
}

enum { IN_ARG,IN_FACTOR,OUT_ARG };

int args[OUT_ARG + 1];

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[IN_ARG] = plugin->regArg("in", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_ARG], "Signal in");
    plugin->setArgRange(args[IN_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[IN_ARG], "full scale");
    args[IN_FACTOR] = plugin->regArg("factor", thPlugin::ARG_IN);
    /* Above 0 the curve bends up towards a square; 0 is the identity; between
       0 and -1 it bends the other way. See FACTOR_MIN for the floor. */
    plugin->setArgDesc(args[IN_FACTOR],
                       "Shapes the curve: the exponent is 1/(factor + 1), "
                       "and 0 passes the signal through");
    plugin->setArgRange(args[IN_FACTOR], FACTOR_MIN, 8);
    plugin->setArgUnits(args[IN_FACTOR], "exponent");
    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The saturated signal");
    plugin->setArgRange(args[OUT_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[OUT_ARG], "full scale");

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out;
    thArg *in_arg, *in_factor;
    thArg *out_arg;
    float factor;
    unsigned int i;
    signed int sign;

    in_arg = mod->getArg(node, args[IN_ARG]);
    in_factor = mod->getArg(node, args[IN_FACTOR]);

    out_arg = mod->getArg(node, args[OUT_ARG]);
    out = out_arg->allocate(windowlen);

    for(i = 0; i < windowlen; i++)
    {
        factor = thClampArg((*in_factor)[i], FACTOR_MIN, 1e6f);
    
        if((*in_arg)[i] >= 0) {
          sign = 1;
        } else {
          sign = -1;
        }
        out[i] = pow(fabs((*in_arg)[i] / TH_MAX), 1/(factor+1)) * TH_MAX * sign;
    }

    return 0;
}
