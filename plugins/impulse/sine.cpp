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

char		*desc = "Generates a sine impulse";
thPluginState	mystate = thPassive;

void module_cleanup (struct module *mod)
{
}

enum { IN_LEN,IN_MAX,IN_PERCENT,OUT_ARG };

int args[OUT_ARG + 1];

int module_init (thPlugin *plugin)
{
	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	args[IN_LEN] = plugin->RegArg("len");
	args[IN_MAX] = plugin->RegArg("max");
	args[IN_PERCENT] = plugin->RegArg("percent");
	args[OUT_ARG] = plugin->RegArg("out");
	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
{
	float *out;
	thArg *in_len, *in_max, *in_percent;
	thArg *out_arg;
	float max, percent, parabolalen, offset, half;
	unsigned int i, len;

	in_len = mod->GetArg(node, args[IN_LEN]);
	in_max = mod->GetArg(node, args[IN_MAX]);
	in_percent = mod->GetArg(node, args[IN_PERCENT]);
	len = (int)(*in_len)[0];
	max = (*in_max)[0];
	percent = (*in_percent)[0];
	half = len/2;

	out_arg = mod->GetArg(node, args[OUT_ARG]);
	out = out_arg->Allocate(len);

	if(percent == 0) {
		percent = 1;
	}
	parabolalen = percent * len;
	offset = (len-parabolalen)/2;
	for(i=0;i<offset;i++) {
		out[i] = 0;
	}
	for(;i<(parabolalen/2)+offset;i++) {
		out[i] = max*sin(i/((float)len-offset)*M_PI);
	}
	for(;i<len;i++) {
		out[i] = out[(int)(half-(i-half))];
	}

	return 0;
}
