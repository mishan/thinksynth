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

char		*desc = "Allpass Filter";
thPlugin::State	mystate = thPlugin::ACTIVE;

void module_cleanup (struct module *mod)
{
}

enum { IN_ARG,IN_FREQ,INOUT_LAST,OUT_ARG };

int args[OUT_ARG + 1];

int module_init (thPlugin *plugin)
{
	plugin->setDesc (desc);
	plugin->setState (mystate);

	args[IN_ARG] = plugin->regArg("in");
	args[IN_FREQ] = plugin->regArg("freq");
	args[INOUT_LAST] = plugin->regArg("last");
	args[OUT_ARG] = plugin->regArg("out");
	return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
					 unsigned int samples)
{
	float *out;
	thArg *in_arg, *in_freq;
	thArg *out_arg;
	thArg *inout_last;
	unsigned int i;
	float *last;
	float in, omega, a0, freq;

	in_arg = mod->getArg(node, args[IN_ARG]);
	in_freq = mod->getArg(node, args[IN_FREQ]);
	/* IIR delay buffer for in and out */
	inout_last = mod->getArg(node, args[INOUT_LAST]);
	last = inout_last->allocate(2);

	out_arg = mod->getArg(node, args[OUT_ARG]);
	out = out_arg->allocate(windowlen);

	for(i = 0; i < windowlen; i++) {
		in = (*in_arg)[i];
		freq = (*in_freq)[i];

		omega = M_PI * freq / samples;
		a0 = -(1.0 - omega) / (1.0 + omega);
		out[i] = a0 * (in - last[0]) + last[1];
		last[0] = out[i];
		last[1] = in;

		/* allpass algorithm from krodokov on #musicdsp
		omega = pi * freq / samplerate;
		a0 = -(1.0 - omega) / (1.0 + omega);
		Out[0] = a0 * (In[0] - Out[1]) + In[1];
		Out[1] = Out[0]; In[1] = In[0];
		*/

		//printf("%f: %f \t%f\n", freq, in, out[i]);
	}

	return 0;
}
