/* $Id: divbuf.cpp,v 1.8 2004/09/08 22:32:51 misha Exp $ */
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

#define SQR(a) (a*a)

char		*desc = "Cheap IIR-ish Filter";
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
	float *out_buf;
	thArg *in_arg, *in_factor;
	thArg *out_arg;
	thArg *inout_buffer;
	
	unsigned int i;
	float factor, buffer;

	out_arg = mod->GetArg(node, "out");
	inout_buffer = mod->GetArg(node, "buffer");
	buffer = (*inout_buffer)[0];
	out = out_arg->Allocate(windowlen);
	out_buf = inout_buffer->Allocate(1);

	in_arg = mod->GetArg(node, "in");
	in_factor = mod->GetArg(node, "factor");

	for(i=0;i<windowlen;i++) {
	  factor = SQR(SQR((*in_factor)[i]));

	  buffer = ((*in_arg)[i] * factor) + (buffer * (1-factor));

	  out[i] = buffer;
	}

	out_buf[0] = buffer;

/*	node->SetArg("out", out, windowlen);
	node->SetArg("buffer", out_buffer, 1); */

	return 0;
}

