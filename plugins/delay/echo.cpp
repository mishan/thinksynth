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

char		*desc = "Echo (echo echo echo)";
thPluginState	mystate = thActive;

void module_cleanup (struct module *mod)
{
}

enum { IN_ARG,IN_SIZE,IN_DELAY,IN_FEEDBACK,IN_DRY,INOUT_BUFFER,INOUT_BUFPOS,OUT_ARG };

int args[OUT_ARG + 1];

int module_init (thPlugin *plugin)
{
	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	args[IN_ARG] = plugin->RegArg("in");
	args[IN_SIZE] = plugin->RegArg("size");
	args[IN_DELAY] = plugin->RegArg("delay");
	args[IN_FEEDBACK] = plugin->RegArg("feedback");
	args[IN_DRY] = plugin->RegArg("dry");
	args[INOUT_BUFFER] = plugin->RegArg("buffer");
	args[INOUT_BUFPOS] = plugin->RegArg("bufpos");
	args[OUT_ARG] = plugin->RegArg("out");
	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
{
	float *out;
	float *buffer, *bufpos;
	thArg *in_arg, *in_size, *in_delay, *in_feedback, *in_dry;
	thArg *out_arg;
	thArg *inout_buffer, *inout_bufpos;
	unsigned int i;
	int index;
	float delay, feedback, dry, in;

	in_arg = mod->GetArg(node, args[IN_ARG]);
	in_size = mod->GetArg(node, args[IN_SIZE]);
	in_delay = mod->GetArg(node, args[IN_DELAY]);
	in_feedback = mod->GetArg(node, args[IN_FEEDBACK]);
	/* How much of the origional signal is passed */
	in_dry = mod->GetArg(node, args[IN_DRY]);

	inout_buffer = mod->GetArg(node, args[INOUT_BUFFER]);
	inout_bufpos = mod->GetArg(node, args[INOUT_BUFPOS]);
	bufpos = inout_bufpos->Allocate(1);

	out_arg = mod->GetArg(node, args[OUT_ARG]);
	out = out_arg->Allocate(windowlen);

	for(i = 0; i < windowlen; i++) {
		unsigned int mySize = (int)(*in_size)[i];
		unsigned int myBufpos = (int)*bufpos;

		buffer = inout_buffer->Allocate(mySize);
		in = (*in_arg)[i];
		feedback = (*in_feedback)[i];
		dry = (*in_dry)[i];

		unsigned int inOutLen = inout_buffer->getLen();

		if(myBufpos > inOutLen) {
			myBufpos = 0;
		}
		index = (int)(myBufpos - (*in_delay)[i]);
		while(index < 0) {
			index += inOutLen;
		}
		delay = buffer[index];

		buffer[myBufpos] = (feedback * delay) + ((1-feedback) * in);

		out[i] = ((1 - dry) * delay) + (dry * in);
		++myBufpos;
		*bufpos = (float)myBufpos;
	}

	return 0;
}
