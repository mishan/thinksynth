/* $Id: static.cpp,v 1.18 2004/04/08 00:34:56 misha Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

char		*desc = "Produces Random Signal";
thPluginState	mystate = thActive;

void module_cleanup (struct module *mod)
{
	printf("Static module unloading\n");
}

/* ModuleLoad() invokes this function with a pointer to the plugin
 * instance. */
int module_init (thPlugin *plugin)
{
	printf("Static plugin loaded\n");
	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	int i;
	float *out;
	float *out_last;
	float position, last;
	thArg* in_sample;  /* How often to change the random number */
	thArg* out_arg;
	thArg* inout_last;  /* So sample can be consistant over windows */

	out_arg = mod->GetArg(node, "out");
	inout_last = mod->GetArg(node, "last");
	position = (*inout_last)[0];
	last = (*inout_last)[1];
	out_last = inout_last->Allocate(1);
	out = out_arg->Allocate(windowlen);

	in_sample = mod->GetArg(node, "sample");

	for(i=0; i < (int)windowlen; i++) {
		if(++position > (*in_sample)[i]) {
			out[i] = TH_RANGE*(rand()/(RAND_MAX+1.0))+TH_MIN;
			position = 0;
			last = out[i];
		} else {
			out[i] = last;
		}
	}

	out_last[0] = position;
	out_last[1] = last;
/*	node->SetArg("out", out, windowlen);
	node->SetArg("last", last, 2);
*/
	return 0;
}
