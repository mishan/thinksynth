/* $Id: fir.cpp,v 1.14 2004/09/08 22:32:51 misha Exp $ */
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
	thArg *in_arg, *in_impulse, *in_mix;
	thArg *out_arg;
	thArg *inout_buffer, *inout_bufpos;
	unsigned int i, j;
	int index;
	float impulse, mix;

	in_arg = mod->GetArg(node, "in");
	in_impulse = mod->GetArg(node, "impulse");
	in_mix = mod->GetArg(node, "mix");

	inout_buffer = mod->GetArg(node, "buffer");
	inout_bufpos = mod->GetArg(node, "bufpos");
	buffer = inout_buffer->Allocate(in_impulse->argNum);
	bufpos = inout_bufpos->Allocate(1);

	out_arg = mod->GetArg(node, "out");
	out = out_arg->Allocate(windowlen);

	for(i=0;i<windowlen;i++) {
		out[i] = 0;

		if(*bufpos > inout_buffer->argNum) {
			*bufpos = 0;
		}
		buffer[(int)*bufpos] = (*in_arg)[i];

		for(j = 0; j < in_impulse->argNum; j++) {
			index = (int)*bufpos - j;
			if(index < 0) {
				index += in_impulse->argNum;
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

