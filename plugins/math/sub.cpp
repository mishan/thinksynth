/* $Id: sub.cpp,v 1.4 2003/05/24 09:17:29 ink Exp $ */

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

char		*desc = "Subtracts two streams";
thPluginState	mystate = thPassive;

extern "C" int	module_init (thPlugin *plugin);
extern "C" int	module_callback (thNode *node, thMod *mod, unsigned int windowlen);
extern "C" void module_cleanup (struct module *mod);

void module_cleanup (struct module *mod)
{
	printf("Subtraction plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("Subtraction plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	float *out ;
	thArgValue *in_0, *in_1;
	thArgValue *out_arg;
	unsigned int i;

	in_0 = (thArgValue *)mod->GetArg(node, "in0");
	in_1 = (thArgValue *)mod->GetArg(node, "in1");

	out_arg = (thArgValue *)mod->GetArg(node, "out");
	out = out_arg->allocate(windowlen);

	for(i=0;i<windowlen;i++) {
		out[i] = (*in_0)[i]-(*in_1)[i];
	}

	return 0;
}

