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

#include "think.h"

char		*desc = "Produces Random Signal";
thPluginState	mystate = thActive;

void module_cleanup (struct module *mod)
{
}

/* ModuleLoad() invokes this function with a pointer to the plugin
 * instance. */
enum { OUT_ARG,INOUT_LAST,IN_SAMPLE };

int args[IN_SAMPLE + 1];

int module_init (thPlugin *plugin)
{
	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	args[OUT_ARG] = plugin->RegArg("out");
	args[INOUT_LAST] = plugin->RegArg("last");
	args[IN_SAMPLE] = plugin->RegArg("sample");
	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
{
	int i;
	float *out;
	float *out_last;
	float position, last;
	thArg* in_sample;  /* How often to change the random number */
	thArg* out_arg;
	thArg* inout_last;  /* So sample can be consistant over windows */

	out_arg = mod->GetArg(node, args[OUT_ARG]);
	inout_last = mod->GetArg(node, args[INOUT_LAST]);
	position = (*inout_last)[0];
	last = (*inout_last)[1];
	out_last = inout_last->Allocate(1);
	out = out_arg->Allocate(windowlen);

	in_sample = mod->GetArg(node, args[IN_SAMPLE]);

	for(i=0; i < (int)windowlen; i++) {
		if(++position > (*in_sample)[i]) {
			out[i] = TH_RANGE*(rand()/(RAND_MAX+1.0))+TH_MIN;
			position = 0;
			last = out[i];
		} else {
			out[i] = last;
		}
	}

	out_last[0] = position;
	out_last[1] = last;
/*	node->SetArg("out", out, windowlen);
	node->SetArg("last", last, 2);
*/
	return 0;
}
