/* $Id: midi2freq.cpp,v 1.5 2003/05/17 16:01:22 ink Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

#include "thArg.h"
#include "thList.h"
#include "thBSTree.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thMod.h"
#include "thSynth.h"

char		*desc = "Converts a midi note value to it's respective frequency";
thPluginState	mystate = thPassive;

extern "C" int	module_init (thPlugin *plugin);
extern "C" int	module_callback (thNode *node, thMod *mod, unsigned int windowlen);
extern "C" void module_cleanup (struct module *mod);

void module_cleanup (struct module *mod)
{
	printf("Midi2Freq plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("Midi2Freq plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	float *out;
	thArgValue *in_note;
	thArgValue *out_arg;
	unsigned int i;

	out_arg = (thArgValue *)mod->GetArg(node, "out");
	out = out_arg->allocate(windowlen);

	in_note = (thArgValue *)mod->GetArg(node, "note");

	for(i=0;i<windowlen;i++) {
	  out[i] = 440*pow(2,((*in_note)[i]-69)/12);
	}

/*	node->SetArg("out", out, windowlen); */
	return 0;
}

