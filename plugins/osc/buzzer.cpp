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
#include <time.h>
#include <math.h>

#include "think.h"

char		*desc = "Buzzer oscillator";
thPluginState	mystate = thActive;

enum { OUT_ARG, IN_FREQ, IN_FACTOR, INOUT_LAST };

int args[INOUT_LAST + 1];

void module_cleanup (struct module *mod)
{
}

/* ModuleLoad() invokes this function with a pointer to the plugin
 * instance. */
int module_init (thPlugin *plugin)
{
	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	args[OUT_ARG] = plugin->RegArg("out");
	args[IN_FREQ] = plugin->RegArg("freq");
	args[IN_FACTOR] = plugin->RegArg("factor");
	args[INOUT_LAST] = plugin->RegArg("last");

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
{
	int i;
	float *out;
	float *out_last;
	float wavelength;
	float position, posratio;
	thArg *in_freq, *in_factor;
	thArg *out_arg;
	thArg *inout_last;

	out_arg = mod->GetArg(node, args[OUT_ARG]);
	inout_last = mod->GetArg(node, args[INOUT_LAST]);

	position = (*inout_last)[0]; /* Where in the phase we are */
	out_last = inout_last->Allocate(1);
	out = out_arg->Allocate(windowlen);

	in_freq = mod->GetArg(node, args[IN_FREQ]);
	in_factor = mod->GetArg(node, args[IN_FACTOR]); // (1-abs(x^factor))*x

	for(i=0; i < (int)windowlen; i++) {
		wavelength = samples * (1.0/(*in_freq)[i]);

		posratio = 2*(position/wavelength)-1;
		out[i] = (pow(fabs(posratio), (*in_factor)[i]))*posratio*TH_MAX;
		//	printf("%f \t%f\n", position, out[i]);
		if(++position > wavelength) {
		  position = 0;
		}
	}
	
	out_last[0] = position;
	
	return 0;
}
