/* $Id: blackman.cpp,v 1.7 2004/09/08 22:32:52 misha Exp $ */
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
	thArg *in_len, *in_cut;
	thArg *out_arg;
	float factor, len, k = 0;
	unsigned int i;

	in_len = mod->GetArg(node, "len"); /* Length of output */
	in_cut = mod->GetArg(node, "cutoff"); /* 0 - 1 */
	len = (int)(*in_len)[0];
	factor = (*in_cut)[0]/2;

	out_arg = mod->GetArg(node, "out");
	out = out_arg->Allocate((int)len+1);
	
	for(i=0;i<=(unsigned int)len;i++) {
		if(i == len/2) {
			out[i] = 2*M_PI*factor;
		} else {
			out[i] = (sin(2*M_PI*factor*(i-len/2))/(i-len/2))*(0.42-0.5*cos((2*M_PI*i)/len)+0.08*cos((4*M_PI*i)/len));
		}
		k += out[i];  /* We need to make all the samples add up to 1 */
	}

	k = 1/k;
	for(i=0;i<len;i++) {
		out[i] *= k;
	}

	return 0;
}

