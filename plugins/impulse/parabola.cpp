/* $Id: parabola.cpp,v 1.1 2003/05/23 23:44:39 ink Exp $ */

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

#define SQR(x) ((x)*(x))

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
	thArgValue *in_len, *in_max;
	thArgValue *out_arg;
	float max;
	unsigned int i, len;

	in_len = (thArgValue *)mod->GetArg(node, "len");
	in_max = (thArgValue *)mod->GetArg(node, "max");
	len = (int)(*in_len)[0];
	max = (*in_max)[0];

	out_arg = (thArgValue *)mod->GetArg(node, "out");
	out = out_arg->allocate(len);

	for(i=0;i<len;i++) {
		out[i] = max*(1-SQR((i/(float)len)*2-1));
	}

	return 0;
}

