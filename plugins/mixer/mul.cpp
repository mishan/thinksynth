/* $Id: mul.cpp,v 1.17 2004/05/26 00:14:04 misha Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

enum {IN_0, IN_1, OUT_ARG};
int args[OUT_ARG + 1];

char		*desc = "Multiplies two streams";
thPluginState	mystate = thPassive;

void module_cleanup (struct module *mod)
{
	printf("Multiplier plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("Multiplier plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	args[IN_0] = plugin->RegArg("in0");
	args[IN_1] = plugin->RegArg("in1");
	args[OUT_ARG] = plugin->RegArg("out");

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
{
	float *out;
	thArg *in_0, *in_1;
	thArg *out_arg;
	float buf_in_0[windowlen], buf_in_1[windowlen];
	unsigned int i;

	in_0 = mod->GetArg(node, args[IN_0]);
	in_1 = mod->GetArg(node, args[IN_1]);

	in_0->GetBuffer(buf_in_0, windowlen);
	in_1->GetBuffer(buf_in_1, windowlen);

	out_arg = mod->GetArg(node, args[OUT_ARG]);
	out = out_arg->Allocate(windowlen);

	for(i=0;i<windowlen;i++) {
		out[i] = buf_in_0[i]*(buf_in_1[i]/TH_MAX);
	}

	return 0;
}

