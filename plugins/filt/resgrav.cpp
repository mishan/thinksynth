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

#define SQR(x) ((x)*(x))

char		*desc = "`INK Filter`  Gravity-based low pass";
thPluginState	mystate = thActive;

void module_cleanup (struct module *mod)
{
}

enum { OUT_ARG,OUT_ACCEL,INOUT_LAST,IN_ARG,IN_CUTOFF,IN_RES };

int args[IN_RES + 1];

int module_init (thPlugin *plugin)
{
	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	args[OUT_ARG] = plugin->RegArg("out");
	args[OUT_ACCEL] = plugin->RegArg("aout");
	args[INOUT_LAST] = plugin->RegArg("last");
	args[IN_ARG] = plugin->RegArg("in");
	args[IN_CUTOFF] = plugin->RegArg("cutoff");
	args[IN_RES] = plugin->RegArg("res");
	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
{
	float *out;
	float *aout;
	float *out_last;
	thArg *in_arg, *in_cutoff, *in_res;
	thArg *out_arg, *out_accel;
	thArg *inout_last;
	unsigned int i;
	float in, last, accel;

	out_arg = mod->GetArg(node, args[OUT_ARG]);
	out_accel = mod->GetArg(node, args[OUT_ACCEL]);
	inout_last = mod->GetArg(node, args[INOUT_LAST]);

	last = (*inout_last)[0];
	accel = (*inout_last)[1];
	out_last = inout_last->Allocate(2);

	out = out_arg->Allocate(windowlen);
	aout = out_accel->Allocate(windowlen);

	in_arg = mod->GetArg(node, args[IN_ARG]);
	in_cutoff = mod->GetArg(node, args[IN_CUTOFF]);
	in_res = mod->GetArg(node, args[IN_RES]);

	for(i=0;i<windowlen;i++) {
		in = (*in_arg)[i];
		if(last > in) {
			accel -= (*in_cutoff)[i];
		}
		else if (last < in) {
			accel += (*in_cutoff)[i];
		}

		accel *= 1+(-1*SQR((*in_res)[i]-1));
//		printf("%f \t%f\n", (*in_res)[i], 1+(-1*SQR((*in_res)[i]-1)));
		last += accel;

		aout[i] = accel;

		out[i] = last;
	}

	out_last[0] = last;
	out_last[1] = accel;

	return 0;
}
