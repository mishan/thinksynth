/* $Id: map.cpp,v 1.2 2003/05/06 17:38:00 ink Exp $ */

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

char		*desc = "Maps a stream to a new value range";
thPluginState	mystate = thPassive;

extern "C" int	module_init (thPlugin *plugin);
extern "C" int	module_callback (thNode *node, thMod *mod, unsigned int windowlen);
extern "C" void module_cleanup (struct module *mod);

void module_cleanup (struct module *mod)
{
	printf("Map plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("Map plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	float *out = new float[windowlen];
	thArgValue *in_arg, *in_min, *in_max, *out_min, *out_max;
	unsigned int i;
	float percent;

	in_arg = (thArgValue *)mod->GetArg(node->GetName(), "in");
	in_min = (thArgValue *)mod->GetArg(node->GetName(), "inmin");
	in_max = (thArgValue *)mod->GetArg(node->GetName(), "inmax");
	out_min = (thArgValue *)mod->GetArg(node->GetName(), "outmin");
	out_max = (thArgValue *)mod->GetArg(node->GetName(), "outmax");

	for(i=0;i<windowlen;i++) {
	  percent = ((*in_arg)[i]-(*in_min)[i])/((*in_max)[i]-(*in_min)[i]);
	  out[i] = (percent*((*out_max)[i]-(*out_min)[i]))+(*out_min)[i];
	}

	node->SetArg("out", out, windowlen);
	return 0;
}
