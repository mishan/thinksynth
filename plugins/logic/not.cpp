/* $Id: not.cpp,v 1.4 2004/04/08 00:34:56 misha Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

char		*desc = "Logical Not";
thPluginState	mystate = thPassive;

void module_cleanup (struct module *mod)
{
	printf("Logical Not plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("Logical Not plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	float *out;
	thArg *in;
	thArg *out_arg;
	unsigned int i;

	in = mod->GetArg(node, "in");

	out_arg = mod->GetArg(node, "out");
	out = out_arg->Allocate(windowlen);

	for(i=0;i<windowlen;i++) {
		if((*in)[i] > 0) {
			out[i] = 0;
		} else {
			out[i] = 1;
		}
	}

	return 0;
}

