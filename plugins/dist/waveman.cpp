/* $Id: waveman.cpp,v 1.6 2004/05/26 00:14:04 misha Exp $ */

/* Algorithm taken from waveman in #musicdsp on efnet */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

char		*desc = "Applies waveman's waveshaper";
thPluginState	mystate = thPassive;

void module_cleanup (struct module *mod)
{
	printf("Waveman plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("Waveman plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
{
	float *out;
	thArg *in_arg, *in_gain;
	thArg *out_arg;
	float in, gain;
	unsigned int i;

	in_arg = mod->GetArg(node, "in");
	in_gain = mod->GetArg(node, "gain");

	out_arg = mod->GetArg(node, "out");
	out = out_arg->Allocate(windowlen);

	for(i=0;i<windowlen;i++) {
		in = (*in_arg)[i] / TH_MAX;
		gain = (*in_gain)[i];
		out[i] = ((((in / (gain + fabs(in))) + ((1.5 * in) - ((0.7 * in) * (0.6 * in) * (0.5 * in) * (0.4 * in) * (0.3 * in) * (0.2 * in) * (0.1 * in)))) / 3.5) / (gain + 1)) * TH_MAX;
	}

	return 0;
}

