/* $Id: noisegate.cpp,v 1.4 2004/05/26 00:14:04 misha Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

char		*desc = "Zeros the output if the input goes below a certain level";
thPluginState	mystate = thActive;

void module_cleanup (struct module *mod)
{
	printf("Noise Gate plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("Noise Gate plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
{
	float *out, *out_last;
	thArg *in_arg, *in_falloff, *in_cutoff;
	thArg *out_arg;
	thArg *inout_last;
	unsigned int i;
	float last, in, absolute, falloff;

	out_arg = mod->GetArg(node, "out");
	inout_last = mod->GetArg(node, "last");

	last = (*inout_last)[0];

	out_last = inout_last->Allocate(1);

	out = out_arg->Allocate(windowlen);

	in_arg = mod->GetArg(node, "in");
	in_falloff = mod->GetArg(node, "falloff");
	in_cutoff = mod->GetArg(node, "cutoff");

	for(i = 0; i < windowlen; i++)
	{
		in = (*in_arg)[i];
		absolute = fabs(in);
		falloff = pow(0.1, (*in_falloff)[i]);

		last *= 1 - falloff;
		last += absolute * falloff;

		if(last >= (*in_cutoff)[i])
		{
			out[i] = in;
		}
		else
		{
			out[i] = 0;
		}
	}

	out_last[0] = last;

	

	return 0;
}

