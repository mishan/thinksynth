/* $Id: inksat.cpp,v 1.1 2003/09/09 05:04:19 ink Exp $ */

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

char		*desc = "Applies x^(1/y) saturation";
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
	signed int sign;

	in_arg = mod->GetArg(node, "in");
	in_factor = mod->GetArg(node, "factor");

	out_arg = mod->GetArg(node, "out");
	out = out_arg->allocate(windowlen);

	for(i=0;i<windowlen;i++) {
		factor = (*in_factor)[i];
		if((*in_arg)[i] >= 0) {
		  sign = 1;
		} else {
		  sign = -1;
		}
		out[i] = pow(fabs((*in_arg)[i] / TH_MAX), 1/(factor+1)) * TH_MAX * sign;
	}

	return 0;
}
