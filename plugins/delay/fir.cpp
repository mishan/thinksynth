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

char		*desc = "Applies an impulse response";
thPlugin::State	mystate = thPlugin::ACTIVE;

void module_cleanup (struct module *mod)
{
}

enum { IN_ARG,IN_IMPULSE,IN_MIX,INOUT_BUFFER,INOUT_BUFPOS,OUT_ARG };

int args[OUT_ARG + 1];

int module_init (thPlugin *plugin)
{
	plugin->setDesc (desc);
	plugin->setState (mystate);

	args[IN_ARG] = plugin->regArg("in");
	args[IN_IMPULSE] = plugin->regArg("impulse");
	args[IN_MIX] = plugin->regArg("mix");
	args[INOUT_BUFFER] = plugin->regArg("buffer");
	args[INOUT_BUFPOS] = plugin->regArg("bufpos");
	args[OUT_ARG] = plugin->regArg("out");
	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
{
	float *out;
	float *buffer, *bufpos;
	thArg *in_arg, *in_impulse, *in_mix;
	thArg *out_arg;
	thArg *inout_buffer, *inout_bufpos;
	unsigned int i, j;
	int index;
	float impulse, mix;
	unsigned int impulse_len;

	in_arg = mod->getArg(node, args[IN_ARG]);
	in_impulse = mod->getArg(node, args[IN_IMPULSE]);
	in_mix = mod->getArg(node, args[IN_MIX]);

	impulse_len = in_impulse->len();

	inout_buffer = mod->getArg(node, args[INOUT_BUFFER]);
	inout_bufpos = mod->getArg(node, args[INOUT_BUFPOS]);
	buffer = inout_buffer->allocate(impulse_len);
	bufpos = inout_bufpos->allocate(1);

	out_arg = mod->getArg(node, args[OUT_ARG]);
	out = out_arg->allocate(windowlen);

	for(i=0;i<windowlen;i++) {
		out[i] = 0;

		if(*bufpos > inout_buffer->len()) {
			*bufpos = 0;
		}
		buffer[(int)*bufpos] = (*in_arg)[i];

		for(j = 0; j < impulse_len; j++) {
			index = (int)*bufpos - j;
			if(index < 0) {
				index += impulse_len;
			}
			impulse = (*in_impulse)[j];
			if(impulse) {
				out[i] += impulse * buffer[index];
			}
		}
		mix = (*in_mix)[i];
		if(mix) {
			buffer[(int)*bufpos] = (buffer[(int)*bufpos] * (1-mix)) + (out[i] * mix); // recursion!
		}
		(*bufpos)++;
	}

	return 0;
}
