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

char		*desc = "Follows the envelope of the input";
thPluginState	mystate = thActive;

void module_cleanup (struct module *mod)
{
}

enum { OUT_ARG,INOUT_LAST,IN_ARG,IN_FALLOFF };

int args[IN_FALLOFF + 1];

int module_init (thPlugin *plugin)
{
	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	args[OUT_ARG] = plugin->RegArg("out");
	args[INOUT_LAST] = plugin->RegArg("last");
	args[IN_ARG] = plugin->RegArg("in");
	args[IN_FALLOFF] = plugin->RegArg("falloff");
	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
{
	float *out, *out_last;
	thArg *in_arg, *in_falloff;
	thArg *out_arg;
	thArg *inout_last;
	unsigned int i;
	float last, absolute, falloff;

	out_arg = mod->GetArg(node, args[OUT_ARG]);
	inout_last = mod->GetArg(node, args[INOUT_LAST]);

	last = (*inout_last)[0];

	out_last = inout_last->Allocate(1);

	out = out_arg->Allocate(windowlen);

	in_arg = mod->GetArg(node, args[IN_ARG]);
	in_falloff = mod->GetArg(node, args[IN_FALLOFF]);


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
