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

char		*desc = "Difference Scaling Filter";
thPluginState	mystate = thActive;

void module_cleanup (struct module *mod)
{
}

enum { OUT_ARG,OUT_HIGH,INOUT_LAST,IN_ARG,IN_CUTOFF };

int args[IN_CUTOFF + 1];

int module_init (thPlugin *plugin)
{
	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	args[OUT_ARG] = plugin->RegArg("out");
	args[OUT_HIGH] = plugin->RegArg("out_high");
	args[INOUT_LAST] = plugin->RegArg("last");
	args[IN_ARG] = plugin->RegArg("in");
	args[IN_CUTOFF] = plugin->RegArg("cutoff");
	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
{
	float *out;
	float *highout;
	float *out_last;
	thArg *in_arg, *in_cutoff;
	thArg *out_arg, *out_high;
	thArg *inout_last;
	unsigned int i;
	float last, diff;
	float fact;

	out_arg = mod->GetArg(node, args[OUT_ARG]);
	out_high = mod->GetArg(node, args[OUT_HIGH]);
	inout_last = mod->GetArg(node, args[INOUT_LAST]);

	last = (*inout_last)[0];
	out_last = inout_last->Allocate(1);

	out = out_arg->Allocate(windowlen);
	highout = out_high->Allocate(windowlen);

	in_arg = mod->GetArg(node, args[IN_ARG]);
	in_cutoff = mod->GetArg(node, args[IN_CUTOFF]);

	for(i=0;i<windowlen;i++) {
	  fact = (*in_cutoff)[i]; //1-(SQR((*in_cutoff)[i]));

	  diff = (*in_arg)[i] - last;
	  if(fabs(diff) > TH_RANGE) { /* in case the input is screwy */
		  diff = TH_RANGE * (diff > 0 ? 1 : -1);
	  }

	  highout[i] = diff/2;
	  diff *= fact;
	  last += diff;

	  out[i] = last;
	}

	out_last[0] = last;

	return 0;
}
