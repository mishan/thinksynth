/* $Id: fade.cpp,v 1.13 2004/09/08 22:32:52 misha Exp $ */
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

enum {IN_0, IN_1, IN_FADE, OUT};
int args[OUT+1];

char		*desc = "Fades between two streams";
thPluginState	mystate = thPassive;

void module_cleanup (struct module *mod)
{
}

int module_init (thPlugin *plugin)
{
	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	args[IN_0] = plugin->RegArg("in0");
	args[IN_1] = plugin->RegArg("in1");
	args[IN_FADE] = plugin->RegArg("fade");
	args[OUT] = plugin->RegArg("out");

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
{
	float *out;
	thArg *in_0, *in_1, *in_fade;
	thArg *out_arg;
	float buf_in0[windowlen], buf_in1[windowlen], buf_fade[windowlen];
	unsigned int i;
	float val_fade;

	in_0 = mod->GetArg(node, args[IN_0]);
	in_1 = mod->GetArg(node, args[IN_1]);
	in_fade = mod->GetArg(node, args[IN_FADE]);

	in_0->GetBuffer(buf_in0, windowlen);
	in_1->GetBuffer(buf_in1, windowlen);
	in_fade->GetBuffer(buf_fade, windowlen);

	out_arg = mod->GetArg(node, args[OUT]);
	out = out_arg->Allocate(windowlen);

	for(i = 0; i < windowlen; i++) {
		val_fade = buf_fade[i];

		out[i] = (buf_in0[i] * (1-val_fade)) + (buf_in1[i] * val_fade);
	}

	return 0;
}
