/* $Id: sinsaw.cpp,v 1.1 2003/09/12 10:02:28 ink Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "think.h"

#include "thArg.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thMod.h"
#include "thSynth.h"

char		*desc = "Sin-Saw oscillator";
thPluginState	mystate = thActive;

extern "C" int	module_init (thPlugin *plugin);
extern "C" int	module_callback (thNode *node, thMod *mod, unsigned int windowlen);
extern "C" void module_cleanup (struct module *mod);

void module_cleanup (struct module *mod)
{
	printf("SinSaw module unloading\n");
}

/* ModuleLoad() invokes this function with a pointer to the plugin
 * instance. */
int module_init (thPlugin *plugin)
{
	printf("SinSaw plugin loaded\n");
	plugin->SetDesc (desc);
	plugin->SetState (mystate);
	
	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	int i;
	float *out;
	float *out_last;
	float wavelength;
	float position, posratio;
	thArg *in_freq, *in_factor;
	thArg *out_arg;
	thArg *inout_last;

	out_arg = mod->GetArg(node, "out");
	inout_last = mod->GetArg(node, "last");

	position = (*inout_last)[0]; /* Where in the phase we are */
	out_last = inout_last->allocate(1);
	out = out_arg->allocate(windowlen);

	in_freq = mod->GetArg(node, "freq");
	in_factor = mod->GetArg(node, "factor"); // (1-abs(x^factor))*x

	for(i=0; i < (int)windowlen; i++) {
		wavelength = TH_SAMPLE * (1.0/(*in_freq)[i]);

		posratio = 2*(position/wavelength)-1;
		out[i] = (1-pow(fabs(posratio), (*in_factor)[i]))*posratio*TH_MAX;
		//	printf("%f \t%f\n", position, out[i]);
		if(++position > wavelength) {
		  position = 0;
		}
	}
	
	out_last[0] = position;
	
	return 0;
}
