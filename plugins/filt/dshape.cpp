/* $Id$ */
/*
 * Copyright (C) 2004 Metaphonic Labs
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

char		*desc = "Resonant Difference Shaping Filter";
thPlugin::State	mystate = thPlugin::ACTIVE;

void module_cleanup (struct module *mod)
{
}

enum { OUT_ARG,OUT_HIGH,INOUT_LAST,IN_ARG,IN_CUTOFF,IN_RES,IN_FACTOR };

int args[IN_FACTOR + 1];

int module_init (thPlugin *plugin)
{
	plugin->setDesc (desc);
	plugin->setState (mystate);

	args[OUT_ARG] = plugin->regArg("out");
	args[OUT_HIGH] = plugin->regArg("out_high");
	args[INOUT_LAST] = plugin->regArg("last");
	args[IN_ARG] = plugin->regArg("in");
	args[IN_CUTOFF] = plugin->regArg("cutoff");
	args[IN_RES] = plugin->regArg("res");
	args[IN_FACTOR] = plugin->regArg("factor");

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
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
	out_last = inout_last->Allocate(2);

	out = out_arg->Allocate(windowlen);
	highout = out_high->Allocate(windowlen);

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
