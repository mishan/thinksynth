/* $Id: add.cpp,v 1.5 2003/05/24 08:25:30 ink Exp $ */

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

char		*desc = "Adds two streams";
thPluginState	mystate = thPassive;

extern "C" int	module_init (thPlugin *plugin);
extern "C" int	module_callback (thNode *node, thMod *mod, unsigned int windowlen);
extern "C" void module_cleanup (struct module *mod);

void module_cleanup (struct module *mod)
{
	printf("Addition plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("Addition plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	float *out;
	thArgValue *in_0, *in_1;
	thArgValue *out_arg;
	unsigned int i, largest;

	in_0 = (thArgValue *)mod->GetArg(node, "in0");
	in_1 = (thArgValue *)mod->GetArg(node, "in1");

	largest = in_0->argNum;
	if((unsigned int)in_1->argNum > largest) {
		largest = in_1->argNum;
	}

	out_arg = (thArgValue *)mod->GetArg(node, "out");
	out = out_arg->allocate(largest);

	for(i=0;i<largest;i++) {
		out[i] = ((*in_0)[i]+(*in_1)[i])/2;
	}

	return 0;
}

