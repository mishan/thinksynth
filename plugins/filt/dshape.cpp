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

#define SQR(a) ((a)*(a))

static const char desc[] = "Resonant Difference Shaping Filter";
thPlugin::State    mystate = thPlugin::ACTIVE;

void module_cleanup (thPlugin *plugin)
{
}

enum { OUT_ARG,OUT_HIGH,INOUT_LAST,IN_ARG,IN_CUTOFF,IN_RES,IN_FACTOR };

int args[IN_FACTOR + 1];

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "Low pass");
    plugin->setArgUnits(args[OUT_ARG], "full scale");
    args[OUT_HIGH] = plugin->regArg("out_high", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_HIGH], "The shaped step, before res and cutoff");
    plugin->setArgUnits(args[OUT_HIGH], "full scale");
    args[INOUT_LAST] = plugin->regArg("last", thPlugin::ARG_STATE);
    args[IN_ARG] = plugin->regArg("in", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_ARG], "Signal in");
    plugin->setArgRange(args[IN_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[IN_ARG], "full scale");
    args[IN_CUTOFF] = plugin->regArg("cutoff", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_CUTOFF],
                       "One-pole coefficient: 1 follows the input exactly");
    plugin->setArgRange(args[IN_CUTOFF], 0, 1);
    plugin->setArgUnits(args[IN_CUTOFF], "one-pole coefficient");
    args[IN_RES] = plugin->regArg("res", thPlugin::ARG_IN);
    /* Same dead `prevdiff' as filt::rds -- read at the top of the window and
       written back unchanged -- so res scales the step by res*res rather than
       resonating. Described as it behaves. */
    plugin->setArgDesc(args[IN_RES], "Scales the step by res squared");
    plugin->setArgRange(args[IN_RES], 0, 1);
    args[IN_FACTOR] = plugin->regArg("factor", thPlugin::ARG_IN);
    /* The step is multiplied by (1 - abs(step)/3)^factor, so 0 leaves it
       alone and larger exponents squash a big step harder than a small one. */
    plugin->setArgDesc(args[IN_FACTOR],
                       "How hard a large step is squashed; 0 is not at all");
    plugin->setArgUnits(args[IN_FACTOR], "exponent");

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out;
    float *highout;
    float *out_last;
    thArg *in_arg, *in_cutoff, *in_res, *in_factor;
    thArg *out_arg, *out_high;
    thArg *inout_last;
    unsigned int i;
    float last, diff;
    float prevdiff, rdiff;
    float fact, rfact;

    out_arg = mod->getArg(node, args[OUT_ARG]);
    out_high = mod->getArg(node, args[OUT_HIGH]);
    inout_last = mod->getArg(node, args[INOUT_LAST]);

    last = (*inout_last)[0];
    prevdiff = (*inout_last)[1];
    out_last = inout_last->allocate(2);

    out = out_arg->allocate(windowlen);
    highout = out_high->allocate(windowlen);

    in_arg = mod->getArg(node, args[IN_ARG]);
    in_cutoff = mod->getArg(node, args[IN_CUTOFF]);
    in_res = mod->getArg(node, args[IN_RES]);
    in_factor = mod->getArg(node, args[IN_FACTOR]);

    for(i=0;i<windowlen;i++) {
      fact = (*in_cutoff)[i]; //1-(SQR((*in_cutoff)[i]));
      rfact = 1-(SQR((*in_res)[i]));

      diff = (*in_arg)[i] - last;
      if(fabs(diff) > TH_RANGE) { /* in case the input is screwy */
          diff = TH_RANGE * (diff > 0 ? 1 : -1);
      }
      diff *= pow(1-fabs(diff/(TH_RANGE+1)), (*in_factor)[i]); /* Diff shaping magic */

      highout[i] = diff;
      rdiff = diff - prevdiff;
      rdiff *= rfact;
      diff -= rdiff;
      diff *= fact;
      last += diff;

      out[i] = last;
    }

    out_last[0] = last;
    out_last[1] = prevdiff;

    return 0;
}
