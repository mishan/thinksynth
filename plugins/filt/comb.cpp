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

char		*desc = "Comb Filter";
thPlugin::State	mystate = thPlugin::ACTIVE;

enum {IN_ARG, IN_FREQ, IN_FEEDBACK, IN_SIZE, OUT_ARG, INOUT_BUFFER,
	  INOUT_BUFPOS};
int args[INOUT_BUFPOS + 1];

void module_cleanup (struct module *mod)
{
}

int module_init (thPlugin *plugin)
{
	plugin->setDesc (desc);
	plugin->setState (mystate);

	args[IN_ARG] = plugin->regArg("in");
	args[IN_FREQ] = plugin->regArg("freq");
	args[IN_FEEDBACK] = plugin->regArg("feedback");
	args[IN_SIZE] = plugin->regArg("size");

	args[OUT_ARG] = plugin->regArg("out");

	args[INOUT_BUFFER] = plugin->regArg("buffer");
	args[INOUT_BUFPOS] = plugin->regArg("bufpos");

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
{
	float *out;
	float *buffer, *bufpos;
	thArg *in_arg, *in_size, *in_freq, *in_feedback;
	thArg *out_arg;
	thArg *inout_buffer, *inout_bufpos;
	unsigned int i;
	float period;
	float buf_in[windowlen], buf_size[windowlen], buf_freq[windowlen],
		buf_feedback[windowlen];

	out_arg = mod->getArg(node, args[OUT_ARG]);
	out = out_arg->Allocate(windowlen);

	in_arg = mod->getArg(node, args[IN_ARG]);
	in_size = mod->getArg(node, args[IN_SIZE]); /* Buffer size */
	in_freq = mod->getArg(node, args[IN_FREQ]); /* Delay length spacing */
	in_feedback = mod->getArg(node, args[IN_FEEDBACK]);

	inout_buffer = mod->getArg(node, args[INOUT_BUFFER]);
	inout_bufpos = mod->getArg(node, args[INOUT_BUFPOS]);
	bufpos = inout_bufpos->Allocate(1);


	in_freq->GetBuffer(buf_freq, windowlen);
	in_arg->GetBuffer(buf_in, windowlen);
	in_feedback->GetBuffer(buf_feedback, windowlen);
	in_size->GetBuffer(buf_size, windowlen);

	buffer = inout_buffer->Allocate((int)buf_size[0]);

	for(i = 0; i < windowlen; i++) {
		period = samples / buf_freq[i];

		if(*bufpos > inout_buffer->getLen()) {
			*bufpos = 0;
		}
		if(*bufpos > period) {
			*bufpos -= period;
		}

		buffer[(int)*bufpos] = buf_feedback[i] * buffer[(int)*bufpos] + buf_in[i];
		out[i] = buffer[(int)*bufpos];

		++(*bufpos);
	}

	return 0;
}

