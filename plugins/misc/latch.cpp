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

char		*desc = "Sample and hold";
thPluginState	mystate = thPassive;

void module_cleanup (struct module *mod)
{
}

enum { INOUT_LAST,IN_ARG,IN_LATCH,OUT_ARG };

int args[OUT_ARG + 1];

int module_init (thPlugin *plugin)
{
	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	args[INOUT_LAST] = plugin->RegArg("last");
	args[IN_ARG] = plugin->RegArg("in");
	args[IN_LATCH] = plugin->RegArg("latch");
	args[OUT_ARG] = plugin->RegArg("out");
	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
{
	float *out, *out_last;
	thArg *in_arg, *in_latch, *inout_last;
	thArg *out_arg;
	unsigned int i;
	float sample;

	inout_last = mod->GetArg(node, args[INOUT_LAST]);
	sample = (*inout_last)[0];
	out_last = inout_last->Allocate(1);

	in_arg = mod->GetArg(node, args[IN_ARG]);
	in_latch = mod->GetArg(node, args[IN_LATCH]);

	out_arg = mod->GetArg(node, args[OUT_ARG]);
	out = out_arg->Allocate(windowlen);

	for(i=0;i<windowlen;i++) {
		if((*in_latch)[i] > 0) {
			sample = (*in_arg)[i];
		}
		out[i] = sample;
	}

	out_last[0] = sample;

	return 0;
}
