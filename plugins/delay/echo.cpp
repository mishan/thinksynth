/* $Id: echo.cpp,v 1.9 2004/09/08 22:32:51 misha Exp $ */
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

int module_init (thPlugin *plugin)
{
	plugin->SetDesc (desc);
	plugin->SetState (mystate);

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

	in_arg = mod->GetArg(node, "in");
	in_size = mod->GetArg(node, "size"); /* Buffer size */
	in_delay = mod->GetArg(node, "delay"); /* Delay length */
	in_feedback = mod->GetArg(node, "feedback");
	/* How much of the origional signal is passed */
	in_dry = mod->GetArg(node, "dry"); 

	inout_buffer = mod->GetArg(node, "buffer");
	inout_bufpos = mod->GetArg(node, "bufpos");
	bufpos = inout_bufpos->Allocate(1);

	out_arg = mod->GetArg(node, "out");
	out = out_arg->Allocate(windowlen);

	for(i = 0; i < windowlen; i++) {
		unsigned int mySize = (int)(*in_size)[i];
		unsigned int myBufpos = (int)*bufpos;

		buffer = inout_buffer->Allocate(mySize);
		in = (*in_arg)[i];
		feedback = (*in_feedback)[i];
		dry = (*in_dry)[i];

		if(myBufpos > inout_buffer->argNum) {
			myBufpos = 0;
		}
		index = (int)(myBufpos - (*in_delay)[i]);
		while(index < 0) {
			index += inout_buffer->argNum;
		}
		delay = buffer[index];

		buffer[myBufpos] = (feedback * delay) + ((1-feedback) * in);

		out[i] = ((1 - dry) * delay) + (dry * in);
		++myBufpos;
		*bufpos = (float)myBufpos;
	}

	return 0;
}

