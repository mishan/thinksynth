/* $Id: followavg.cpp,v 1.3 2004/04/08 00:34:56 misha Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

char		*desc = "Follows the envelope of the input";
thPluginState	mystate = thActive;

void module_cleanup (struct module *mod)
{
	printf("Avg Envelope Follower plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("Avg Envelope Follower plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	float *out, *out_last;
	thArg *in_arg, *in_falloff;
	thArg *out_arg;
	thArg *inout_last;
	unsigned int i;
	float last, absolute, falloff;

	out_arg = mod->GetArg(node, "out");
	inout_last = mod->GetArg(node, "last");

	last = (*inout_last)[0];

	out_last = inout_last->Allocate(1);

	out = out_arg->Allocate(windowlen);

	in_arg = mod->GetArg(node, "in");
	in_falloff = mod->GetArg(node, "falloff");


	for(i = 0; i < windowlen; i++)
	{
		absolute = fabs((*in_arg)[i]);
		falloff = pow(0.1, (*in_falloff)[i]);

		last *= 1 - falloff;
		last += absolute * falloff;

		out[i] = last;
	}

	out_last[0] = last;

	

	return 0;
}

