/*
 * Copyright (C) 2004-2014 Metaphonic Labs
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
/* Written by Leif Ames <ink@bespni.org>
   Algorithm taken from musicdsp.org posted by Paul Kellett */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

#define SQR(x) ((x) * (x))

enum {IN_ARG, IN_CUTOFF, IN_RES, OUT_ARG, INOUT_BUFFER};
int args[INOUT_BUFFER + 1];

char		*desc = "INK Filter ][";
thPlugin::State	mystate = thPlugin::ACTIVE;

void module_cleanup (struct module *mod)
{
}

int module_init (thPlugin *plugin)
{
	plugin->setDesc (desc);
	plugin->setState (mystate);

	args[IN_ARG] = plugin->regArg("in");
	args[IN_CUTOFF] = plugin->regArg("cutoff");
	args[IN_RES] = plugin->regArg("res");

	args[OUT_ARG] = plugin->regArg("out");

	args[INOUT_BUFFER] = plugin->regArg("buffer");

	return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
					 unsigned int samples)
{
	float *out, *buffer;
	thArg *in_arg, *in_cutoff, *in_res;
	thArg *out_arg;
	thArg *inout_buffer;
	float buf0, buf1, in;
	float f, q;  /* cutoff, res */
	unsigned int i;

	out_arg = mod->getArg(node, args[OUT_ARG]);
	out = out_arg->allocate(windowlen);

	inout_buffer = mod->getArg(node, args[INOUT_BUFFER]);
	buf0 = (*inout_buffer)[0];
	buf1 = (*inout_buffer)[1];
	buffer = inout_buffer->allocate(2);

	in_arg = mod->getArg(node, args[IN_ARG]);
	in_cutoff = mod->getArg(node, args[IN_CUTOFF]);
	in_res = mod->getArg(node, args[IN_RES]);

	for(i=0;i<windowlen;i++) {
		f = SQR((*in_cutoff)[i]);
		q = (*in_res)[i];
		in = (*in_arg)[i];

		buf0 *= 1 - f;
		buf0 += (buf1 - in) * f;
		buf1 *= 1 - (f * q);
		buf1 += (in - buf0) * q;

		out[i] = (buf0 - buf1);
	}

	buffer[0] = buf0;
	buffer[1] = buf1;

	return 0;
}

