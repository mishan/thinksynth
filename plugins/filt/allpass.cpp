/* $Id: allpass.cpp,v 1.3 2004/04/08 00:34:56 misha Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

char		*desc = "Allpass Filter";
thPluginState	mystate = thActive;

void module_cleanup (struct module *mod)
{
	printf("Allpass plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("Allpass plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	float *out;
	thArg *in_arg, *in_freq;
	thArg *out_arg;
	thArg *inout_last;
	unsigned int i;
	int index;
	float *last;
	float in, omega, a0, freq;

	in_arg = mod->GetArg(node, "in");
	in_freq = mod->GetArg(node, "freq");

	inout_last = mod->GetArg(node, "last"); /* IIR delay buffer for in and out */
	last = inout_last->Allocate(2);

	out_arg = mod->GetArg(node, "out");
	out = out_arg->Allocate(windowlen);

	for(i = 0; i < windowlen; i++) {
		in = (*in_arg)[i];
		freq = (*in_freq)[i];

		omega = M_PI * freq / TH_SAMPLE;
		a0 = -(1.0 - omega) / (1.0 + omega);
		out[i] = a0 * (in - last[0]) + last[1];
		last[0] = out[i];
		last[1] = in;

		/* allpass algorithm from krodokov on #musicdsp
		omega = pi * freq / samplerate;
		a0 = -(1.0 - omega) / (1.0 + omega);
		Out[0] = a0 * (In[0] - Out[1]) + In[1];
		Out[1] = Out[0]; In[1] = In[0];
		*/

		//printf("%f: %f \t%f\n", freq, in, out[i]);
	}

	return 0;
}

