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

char		*desc = "Converts a frequency to wavelength in samples";
thPlugin::State	mystate = thPlugin::PASSIVE;

void module_cleanup (struct module *mod)
{
}

enum { IN_FREQ,OUT_ARG };

int args[OUT_ARG + 1];

int module_init (thPlugin *plugin)
{
	plugin->setDesc (desc);
	plugin->setState (mystate);

	args[IN_FREQ] = plugin->regArg("freq");
	args[OUT_ARG] = plugin->regArg("out");
	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
{
	float *out;
	thArg *in_freq;
	thArg *out_arg;
	unsigned int i, argnum;

	in_freq = mod->getArg(node, args[IN_FREQ]);

	out_arg = mod->getArg(node, args[OUT_ARG]);
	argnum = (unsigned int) in_freq->getLen();
	out = out_arg->Allocate(argnum);

	for(i=0;i<argnum;i++) {
	  out[i] = (1/(*in_freq)[i])*samples;
	}

/*	node->SetArg("out", out, windowlen); */
	return 0;
}
