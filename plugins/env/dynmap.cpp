/* $Id: dynmap.cpp,v 1.2 2004/05/26 00:14:04 misha Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

enum {IN_INMIN, IN_INMAX, IN_OUTMIN, IN_OUTMAX, IN_ARG, OUT_ARG};
int args[OUT_ARG + 1];

char		*desc = "Maps a stream to a new value range (dynamic)";
thPluginState	mystate = thPassive;

void module_cleanup (struct module *mod)
{
	printf("Dynamic Map plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("Dynamic Map plugin loaded\n");

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

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
{
	float *out;
	thArg *in_arg, *in_min, *in_max, *out_min, *out_max;
	thArg *out_arg;
	unsigned int i;
	float percent;
	float val_imin, val_imax, val_omin, val_omax, val_arg;

	out_arg = mod->GetArg(node, args[OUT_ARG]);
	out = out_arg->Allocate(windowlen);

	in_arg = mod->GetArg(node, args[IN_ARG]);
	in_min = mod->GetArg(node, args[IN_INMIN]);
	in_max = mod->GetArg(node, args[IN_INMAX]);
	out_min = mod->GetArg(node, args[IN_OUTMIN]);
	out_max = mod->GetArg(node, args[IN_OUTMAX]);

	for(i = 0; i < windowlen; i++) {
		val_arg = (*in_arg)[i];
		val_imin = (*in_min)[i];
		val_imax = (*in_max)[i];
		val_omin = (*out_min)[i];
		val_omax = (*out_max)[i];

		percent = (val_arg-val_imin)/(val_imax-val_imin);
		out[i] = (percent*(val_omax-val_omin))+val_omin;
	}

/*	node->SetArg("out", out, windowlen); */
	return 0;
}

