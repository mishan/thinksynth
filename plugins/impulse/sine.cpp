/* $Id: sine.cpp,v 1.5 2004/04/08 00:34:56 misha Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

char		*desc = "Generates a sine impulse";
thPluginState	mystate = thPassive;

void module_cleanup (struct module *mod)
{
	printf("Sine plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("Sine plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	float *out;
	thArg *in_len, *in_max, *in_percent;
	thArg *out_arg;
	float max, percent, parabolalen, offset, half;
	unsigned int i, len;

	in_len = mod->GetArg(node, "len"); /* Length of output */
	in_max = mod->GetArg(node, "max"); /* Sine peak */
	in_percent = mod->GetArg(node, "percent"); /* How much of the output is actually parabola */
	len = (int)(*in_len)[0];
	max = (*in_max)[0];
	percent = (*in_percent)[0];
	half = len/2;

	out_arg = mod->GetArg(node, "out");
	out = out_arg->Allocate(len);

	if(percent == 0) {
		percent = 1;
	}
	parabolalen = percent * len;
	offset = (len-parabolalen)/2;
	for(i=0;i<offset;i++) {
		out[i] = 0;
	}
	for(;i<(parabolalen/2)+offset;i++) {
		out[i] = max*sin(i/((float)len-offset)*M_PI);
	}
	for(;i<len;i++) {
		out[i] = out[(int)(half-(i-half))];
	}

	return 0;
}

