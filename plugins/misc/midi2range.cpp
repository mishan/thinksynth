/* $Id: midi2range.cpp,v 1.2 2004/05/26 00:14:04 misha Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

#define CONSTANT TH_MAX/MIDIVALMAX

enum {IN_ARG, OUT_ARG};
int args[OUT_ARG + 1];

char		*desc = "Maps a midi controller value from 0 to TH_MAX";
thPluginState	mystate = thPassive;

void module_cleanup (struct module *mod)
{
	printf("Midi2Range plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("Midi2Range plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	args[IN_ARG] = plugin->RegArg("in");
	args[OUT_ARG] = plugin->RegArg("out");

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
{
	float *out;
	thArg *in_arg;
	thArg *out_arg;
	unsigned int i, argnum;

	in_arg = mod->GetArg(node, args[IN_ARG]);

	out_arg = mod->GetArg(node, args[OUT_ARG]);
	argnum = (unsigned int) in_arg->argNum;
	out = out_arg->Allocate(argnum);

	for(i=0;i<argnum;i++) {
		out[i] = (*in_arg)[i] * CONSTANT;
	}

	return 0;
}

