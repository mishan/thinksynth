/* $Id: freq2samples.cpp,v 1.4 2004/04/08 00:34:56 misha Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

char		*desc = "Converts a frequency to wavelength in samples";
thPluginState	mystate = thPassive;

void module_cleanup (struct module *mod)
{
	printf("Freq2Samples plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("Freq2Samples plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	float *out;
	thArg *in_freq;
	thArg *out_arg;
	unsigned int i, argnum;

	in_freq = mod->GetArg(node, "freq");

	out_arg = mod->GetArg(node, "out");
	argnum = (unsigned int) in_freq->argNum;
	out = out_arg->Allocate(argnum);

	for(i=0;i<argnum;i++) {
	  out[i] = (1/(*in_freq)[i])*TH_SAMPLE;
	}

/*	node->SetArg("out", out, windowlen); */
	return 0;
}

