/* $Id: fade.cpp,v 1.3 2003/05/24 08:25:30 ink Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

#include "thArg.h"
#include "thList.h"
#include "thBSTree.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thMod.h"
#include "thSynth.h"

char		*desc = "Fades between two streams";
thPluginState	mystate = thPassive;

extern "C" int	module_init (thPlugin *plugin);
extern "C" int	module_callback (thNode *node, thMod *mod, unsigned int windowlen);
extern "C" void module_cleanup (struct module *mod);

void module_cleanup (struct module *mod)
{
	printf("Fade plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("Fade plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	float *out;
	thArgValue *in_0, *in_1, *in_fade;
	thArgValue *out_arg;
	unsigned int i, largest;

	in_0 = (thArgValue *)mod->GetArg(node, "in0");
	in_1 = (thArgValue *)mod->GetArg(node, "in1");
	in_fade = (thArgValue *)mod->GetArg(node, "fade");

	largest = in_0->argNum;
	if((unsigned int)in_1->argNum > largest) {
		largest = in_1->argNum;
	}
	if((unsigned int)in_fade->argNum > largest) {
		largest = in_fade->argNum;
	}

	out_arg = (thArgValue *)mod->GetArg(node, "out");
	out = out_arg->allocate(largest);

	for(i=0;i<largest;i++) {
		out[i] = ((*in_0)[i] * (1-(*in_fade)[i])) + ((*in_1)[i] * (*in_fade)[i]);
	}

/*	node->SetArg("out", out, windowlen); */
	return 0;
}
