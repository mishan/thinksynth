/* $Id: fade.cpp,v 1.11 2004/05/08 23:35:22 ink Exp $ */

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
	float buf_in0[windowlen], buf_in1[windowlen], buf_fade[windowlen];
	unsigned int i;
	float val_fade;

	in_0 = mod->GetArg(node, args[IN_0]);
	in_1 = mod->GetArg(node, args[IN_1]);
	in_fade = mod->GetArg(node, args[IN_FADE]);

	in_0->GetBuffer(buf_in0, windowlen);
	in_1->GetBuffer(buf_in1, windowlen);
	in_fade->GetBuffer(buf_fade, windowlen);

	out_arg = mod->GetArg(node, args[OUT]);
	out = out_arg->Allocate(windowlen);

	for(i = 0; i < windowlen; i++) {
		val_fade = buf_fade[i];

		out[i] = (buf_in0[i] * (1-val_fade)) + (buf_in1[i] * val_fade);
	}

	return 0;
}
