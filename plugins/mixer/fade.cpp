/* $Id: fade.cpp,v 1.10 2004/04/13 10:30:49 misha Exp $ */

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
	float val_in0, val_in1, val_fade;

	in_0 = mod->GetArg(node, args[IN_0]);
	in_1 = mod->GetArg(node, args[IN_1]);
	in_fade = mod->GetArg(node, args[IN_FADE]);

	out_arg = mod->GetArg(node, args[OUT]);
	out = out_arg->Allocate(windowlen);

	for(i = 0; i < windowlen; i++) {
		val_in0 = (*in_0)[i];
		val_in1 = (*in_1)[i];
		val_fade = (*in_fade)[i];

		out[i] = (val_in0 * (1-val_fade)) + (val_in1 * val_fade);
	}

	return 0;
}
