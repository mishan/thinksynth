/* $Id: shapeo.cpp,v 1.5 2004/09/08 22:32:52 misha Exp $ */
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

char		*desc = "Shaped oscillator";
thPluginState	mystate = thActive;

void module_cleanup (struct module *mod)
{
}

/* ModuleLoad() invokes this function with a pointer to the plugin
 * instance. */
int module_init (thPlugin *plugin)
{
	plugin->SetDesc (desc);
	plugin->SetState (mystate);
	
	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
{
	int i;
	float *out;
	float *out_last;
	float wavelength, quarterlength;
	float position;
	int phase;
	thArg *in_freq, *in_shape;
	thArg *out_arg;
	thArg *inout_last;

	out_arg = mod->GetArg(node, "out");
	inout_last = mod->GetArg(node, "last");

	position = (*inout_last)[0]; /* Where in the phase we are */
	phase = (int)(*inout_last)[1]; /* Which phase we are in */
	out_last = inout_last->Allocate(2);
	out = out_arg->Allocate(windowlen);

	in_freq = mod->GetArg(node, "freq");
	in_shape = mod->GetArg(node, "shape"); // Shape Variable

	for(i=0; i < (int)windowlen; i++) {
		wavelength = samples * (1.0/(*in_freq)[i]);
		quarterlength = wavelength/4;

		switch(phase) {
		case 0:    /* First Quarter */
		  out[i] = TH_MAX*pow(position++/quarterlength, (*in_shape)[i]); /* Shaper function */
		  if(position >= quarterlength) { // End when its over
			position -= quarterlength;
			phase++;
			}
		  break;
		case 1:    /* Second Quarter */
		  out[i] = TH_MAX*pow(1-(position++/quarterlength), (*in_shape)[i]); /* Shaper function */
		  if(position >= quarterlength) { // End when its over
			position -= quarterlength;
			phase++;
			}
		  break;
		case 2:    /* Third Quarter */
		  out[i] = TH_MIN*pow(position++/quarterlength, (*in_shape)[i]); /* Shaper function */
		  if(position >= quarterlength) { // End when its over
			position -= quarterlength;
			phase++;
			}
		  break;
		case 3:    /* Fourth Quarter */
		  out[i] = TH_MIN*pow(1-(position++/quarterlength), (*in_shape)[i]); /* Shaper function */
		  if(position >= quarterlength) { // End when its over
			position -= quarterlength;
			phase = 0;
			}
		  break;
		}
	}
	
	out_last[0] = position;
	out_last[1] = (float)phase;
	
	return 0;
}
