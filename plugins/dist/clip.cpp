/* $Id: clip.cpp,v 1.9 2004/09/08 22:32:51 misha Exp $ */
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

char		*desc = "Amplifies and clips the stream";
thPluginState	mystate = thPassive;

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
	float *out ;
	thArg *in_arg, *in_clip, *in_lowclip;
	thArg *out_arg;
	unsigned int i;
	float in, clip, lowclip;

	out_arg = mod->GetArg(node, "out");
	out = out_arg->Allocate(windowlen);

	in_arg = mod->GetArg(node, "in");
	in_clip = mod->GetArg(node, "clip");
	in_lowclip = mod->GetArg(node, "lowclip");

	for(i=0;i<windowlen;i++) {
		in = (*in_arg)[i];
		clip = (*in_clip)[i] * TH_MAX;
		lowclip = (*in_lowclip)[i] * TH_MAX;

		if(clip < 1) {  /* Clip needs to be at least one */
			clip = 1;
		}
		if(lowclip < 1) {  /* Same for the bottom side */
			lowclip = clip;
		}
		lowclip *= -1;

		if(in > clip) {  /* Apply the clipping */
			in = clip;
		}
		else if (in < lowclip) {
			in = lowclip;
		}
		/* Map the clipped data to the data range */
		out[i] = (((in-lowclip)/(clip-lowclip))*TH_RANGE)+TH_MIN;
	}

/*	node->SetArg("out", out, windowlen); */
	return 0;
}

