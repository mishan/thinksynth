/* $Id: mul.cpp,v 1.7 2003/05/17 15:27:30 ink Exp $ */

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

char		*desc = "Multiplies two streams";
thPluginState	mystate = thPassive;

extern "C" int	module_init (thPlugin *plugin);
extern "C" int	module_callback (thNode *node, thMod *mod, unsigned int windowlen);
extern "C" void module_cleanup (struct module *mod);

void module_cleanup (struct module *mod)
{
	printf("Multiplier plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("Multiplier plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	float *out;
	thArgValue *in_0, *in_1;
	thArgValue *out_arg;
	unsigned int i;

	out_arg = (thArgValue *)mod->GetArg(node, "out");
	out = out_arg->allocate(windowlen);

	in_0 = (thArgValue *)mod->GetArg(node, "in0");
	in_1 = (thArgValue *)mod->GetArg(node, "in1");

	for(i=0;i<windowlen;i++) {
		out[i] = (*in_0)[i]*((*in_1)[i]/TH_MAX);
		//	printf("MUL: %f * %f = %f\n", (*in_0)[i], (*in_1)[i], out[i]);
	}

/*	node->SetArg("out", out, windowlen); */
	return 0;
}

