/* $Id: shapeo.cpp,v 1.3 2004/04/08 00:34:56 misha Exp $ */

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
	printf("ShapeO module unloading\n");
}

/* ModuleLoad() invokes this function with a pointer to the plugin
 * instance. */
int module_init (thPlugin *plugin)
{
	printf("ShapeO plugin loaded\n");
	plugin->SetDesc (desc);
	plugin->SetState (mystate);
	
	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
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
		wavelength = TH_SAMPLE * (1.0/(*in_freq)[i]);
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
