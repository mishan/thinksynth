/* $Id: map.cpp,v 1.9 2004/04/09 04:15:09 ink Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

enum {IN_INMIN, IN_INMAX, IN_OUTMIN, IN_OUTMAX, IN_ARG, OUT_ARG};
int args[OUT_ARG + 1];

char		*desc = "Maps a stream to a new value range";
thPluginState	mystate = thPassive;

void module_cleanup (struct module *mod)
{
	printf("Map plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("Map plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	args[IN_INMIN] = plugin->RegArg("inmin");
	args[IN_INMAX] = plugin->RegArg("inmax");
	args[IN_OUTMIN] = plugin->RegArg("outmin");
	args[IN_OUTMAX] = plugin->RegArg("outmax");
	args[IN_ARG] = plugin->RegArg("in");
	args[OUT_ARG] = plugin->RegArg("out");

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	float *out;
	thArg *in_arg, *in_min, *in_max, *out_min, *out_max;
	thArg *out_arg;
	unsigned int i;
	float percent;

	out_arg = mod->GetArg(node, args[OUT_ARG]);
	out = out_arg->Allocate(windowlen);

	in_arg = mod->GetArg(node, args[IN_ARG]);
	in_min = mod->GetArg(node, args[IN_INMIN]);
	in_max = mod->GetArg(node, args[IN_INMAX]);
	out_min = mod->GetArg(node, args[IN_OUTMIN]);
	out_max = mod->GetArg(node, args[IN_OUTMAX]);

	for(i = 0; i < windowlen; i++) {
	  percent = ((*in_arg)[i]-(*in_min)[i])/((*in_max)[i]-(*in_min)[i]);
	  out[i] = (percent*((*out_max)[i]-(*out_min)[i]))+(*out_min)[i];
	}

/*	node->SetArg("out", out, windowlen); */
	return 0;
}

