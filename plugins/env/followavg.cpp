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
thPlugin::State	mystate = thPlugin::ACTIVE;

void module_cleanup (struct module *mod)
{
}

enum { OUT_ARG,INOUT_LAST,IN_ARG,IN_FALLOFF };

int args[IN_FALLOFF + 1];

int module_init (thPlugin *plugin)
{
	plugin->setDesc (desc);
	plugin->setState (mystate);

	args[OUT_ARG] = plugin->regArg("out");
	args[INOUT_LAST] = plugin->regArg("last");
	args[IN_ARG] = plugin->regArg("in");
	args[IN_FALLOFF] = plugin->regArg("falloff");

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

		last *= 1 - falloff;
		last += absolute * falloff;

		out[i] = last;
	}

	out_last[0] = last;

	

	return 0;
}
