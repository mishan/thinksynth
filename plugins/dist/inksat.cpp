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

char		*desc = "Applies x^(1/y) saturation";
thPlugin::State	mystate = thPlugin::PASSIVE;

void module_cleanup (struct module *mod)
{
}

enum { IN_ARG,IN_FACTOR,OUT_ARG };

int args[OUT_ARG + 1];

int module_init (thPlugin *plugin)
{
	plugin->setDesc (desc);
	plugin->setState (mystate);

	args[IN_ARG] = plugin->regArg("in");
	args[IN_FACTOR] = plugin->regArg("factor");
	args[OUT_ARG] = plugin->regArg("out");

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
		factor = (*in_factor)[i];
	
		if((*in_arg)[i] >= 0) {
		  sign = 1;
		} else {
		  sign = -1;
		}
		out[i] = pow(fabs((*in_arg)[i] / TH_MAX), 1/(factor+1)) * TH_MAX * sign;
	}

	return 0;
}
