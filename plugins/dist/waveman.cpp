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

/* Algorithm taken from waveman in #musicdsp on efnet */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

char		*desc = "Applies waveman's waveshaper";
thPlugin::State	mystate = thPlugin::PASSIVE;

void module_cleanup (struct module *mod)
{
}

enum { IN_ARG,IN_GAIN,OUT_ARG };

int args[OUT_ARG + 1];

int module_init (thPlugin *plugin)
{
	plugin->setDesc (desc);
	plugin->setState (mystate);

	args[IN_ARG] = plugin->regArg("in");
	args[IN_GAIN] = plugin->regArg("gain");
	args[OUT_ARG] = plugin->regArg("out");
	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
{
	float *out;
	thArg *in_arg, *in_gain;
	thArg *out_arg;
	float in, gain;
	unsigned int i;

	in_arg = mod->getArg(node, args[IN_ARG]);
	in_gain = mod->getArg(node, args[IN_GAIN]);

	out_arg = mod->getArg(node, args[OUT_ARG]);
	out = out_arg->Allocate(windowlen);

	for(i = 0; i < windowlen; i++)
	{
		in = (*in_arg)[i] / TH_MAX;
		gain = (*in_gain)[i];
		out[i] = ((((in / (gain + fabs(in))) + ((1.5 * in) - ((0.7 * in) * (0.6 * in) * (0.5 * in) * (0.4 * in) * (0.3 * in) * (0.2 * in) * (0.1 * in)))) / 3.5) / (gain + 1)) * TH_MAX;
	}

	return 0;
}
