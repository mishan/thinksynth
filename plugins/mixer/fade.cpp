/* $Id: fade.cpp,v 1.9 2004/04/08 13:44:05 ink Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

enum {IN_0, IN_1, IN_FADE, OUT};
int args[OUT+1];

char		*desc = "Fades between two streams";
thPluginState	mystate = thPassive;

void module_cleanup (struct module *mod)
{
	printf("Fade plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("Fade plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	args[IN_0] = plugin->RegArg("in0");
	args[IN_1] = plugin->RegArg("in1");
	args[IN_FADE] = plugin->RegArg("fade");
	args[OUT] = plugin->RegArg("out");

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	float *out;
	thArg *in_0, *in_1, *in_fade;
	thArg *out_arg;
	unsigned int i;

	in_0 = mod->GetArg(node, args[IN_0]);
	in_1 = mod->GetArg(node, args[IN_1]);
	in_fade = mod->GetArg(node, args[IN_FADE]);

	out_arg = mod->GetArg(node, args[OUT]);
	out = out_arg->Allocate(windowlen);

	for(i=0;i<windowlen;i++) {
		out[i] = ((*in_0)[i] * (1-(*in_fade)[i])) + ((*in_1)[i] * (*in_fade)[i]);
	}

	return 0;
}
