/* $Id: saturate.cpp,v 1.2 2003/05/30 00:55:41 aaronl Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

#include "thArg.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thMod.h"
#include "thSynth.h"

char		*desc = "Applies tanh saturation";
thPluginState	mystate = thPassive;

extern "C" int	module_init (thPlugin *plugin);
extern "C" int	module_callback (thNode *node, thMod *mod, unsigned int windowlen);
extern "C" void module_cleanup (struct module *mod);

void module_cleanup (struct module *mod)
{
	printf("Saturation plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("Saturation plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	float *out;
	thArg *in_arg, *in_factor;
	thArg *out_arg;
	float factor;
	unsigned int i;

	in_arg = mod->GetArg(node, "in");
	in_factor = mod->GetArg(node, "factor");

	out_arg = mod->GetArg(node, "out");
	out = out_arg->allocate(windowlen);

	for(i=0;i<windowlen;i++) {
		factor = (*in_factor)[i];
		out[i] = tanh(factor * ((*in_arg)[i] / TH_MAX)) * TH_MAX;
	}

	return 0;
}
