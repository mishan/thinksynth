/* $Id: map.cpp,v 1.13 2004/09/08 22:32:51 misha Exp $ */
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

enum {IN_INMIN, IN_INMAX, IN_OUTMIN, IN_OUTMAX, IN_ARG, OUT_ARG};
int args[OUT_ARG + 1];

char		*desc = "Maps a stream to a new value range";
thPluginState	mystate = thPassive;

void module_cleanup (struct module *mod)
{
}

int module_init (thPlugin *plugin)
{
	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	args[IN_INMIN] = plugin->RegArg("inmin");
	args[IN_INMAX] = plugin->RegArg("inmax");
	args[IN_OUTMIN] = plugin->RegArg("outmin");
	args[IN_OUTMAX] = plugin->RegArg("outmax");
	args[IN_ARG] = plugin->RegArg("in");
	args[OUT_ARG] = plugin->RegArg("out");

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
{
	float *out;
	thArg *in_arg, *in_min, *in_max, *out_min, *out_max;
	thArg *out_arg;
	float buf_in[windowlen], buf_inmin[windowlen], buf_inmax[windowlen],
		buf_outmin[windowlen], buf_outmax[windowlen];
	unsigned int i;
	float percent;
	float val_imin, val_imax, val_omin, val_omax;

	out_arg = mod->GetArg(node, args[OUT_ARG]);
	out = out_arg->Allocate(windowlen);

	in_arg = mod->GetArg(node, args[IN_ARG]);
	in_min = mod->GetArg(node, args[IN_INMIN]);
	in_max = mod->GetArg(node, args[IN_INMAX]);
	out_min = mod->GetArg(node, args[IN_OUTMIN]);
	out_max = mod->GetArg(node, args[IN_OUTMAX]);

	in_arg->GetBuffer(buf_in, windowlen);
	in_min->GetBuffer(buf_inmin, windowlen);
	in_max->GetBuffer(buf_inmax, windowlen);
	out_min->GetBuffer(buf_outmin, windowlen);
	out_max->GetBuffer(buf_outmax, windowlen);

	for(i = 0; i < windowlen; i++) {
		val_imin = buf_inmin[i];
		val_imax = buf_inmax[i];
		val_omin = buf_outmin[i];
		val_omax = buf_outmax[i];
		
		percent = (buf_in[i]-val_imin)/(val_imax-val_imin);
		out[i] = (percent*(val_omax-val_omin))+val_omin;
	}

/*	node->SetArg("out", out, windowlen); */
	return 0;
}

