/* $Id: div.cpp,v 1.10 2004/04/09 06:33:49 ink Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

enum {IN_0, IN_1, OUT_ARG};
int args[OUT_ARG + 1];

char		*desc = "Divides two streams";
thPluginState	mystate = thPassive;

void module_cleanup (struct module *mod)
{
	printf("Division plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("Division plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	args[IN_0] = plugin->RegArg("in0");
	args[IN_1] = plugin->RegArg("in1");
	args[OUT_ARG] = plugin->RegArg("out");

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	float *out;
	thArg *in_0, *in_1;
	thArg *out_arg;
	unsigned int i;

	in_0 = mod->GetArg(node, args[IN_0]);
	in_1 = mod->GetArg(node, args[IN_1]);

	out_arg = mod->GetArg(node, args[OUT_ARG]);
	out = out_arg->Allocate(windowlen);

	for(i=0;i<windowlen;i++) {
		out[i] = (*in_0)[i]/(*in_1)[i];
	}

/*	node->SetArg("out", out, windowlen); */
	return 0;
}

