/* $Id: map.cpp,v 1.7 2004/03/26 09:50:33 joshk Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

#include "thArg.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thMod.h"
#include "thSynth.h"

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

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	float *out;
	thArg *in_arg, *in_min, *in_max, *out_min, *out_max;
	thArg *out_arg;
	unsigned int i;
	float percent;

	out_arg = mod->GetArg(node, "out");
	out = out_arg->Allocate(windowlen);

	in_arg = mod->GetArg(node, "in");
	in_min = mod->GetArg(node, "inmin");
	in_max = mod->GetArg(node, "inmax");
	out_min = mod->GetArg(node, "outmin");
	out_max = mod->GetArg(node, "outmax");

	for(i = 0; i < windowlen; i++) {
	  percent = ((*in_arg)[i]-(*in_min)[i])/((*in_max)[i]-(*in_min)[i]);
	  out[i] = (percent*((*out_max)[i]-(*out_min)[i]))+(*out_min)[i];
	}

/*	node->SetArg("out", out, windowlen); */
	return 0;
}

