/* $Id: inksat.cpp,v 1.5 2004/05/26 00:14:04 misha Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

char		*desc = "Applies x^(1/y) saturation";
thPluginState	mystate = thPassive;

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

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
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
	out = out_arg->Allocate(windowlen);

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

