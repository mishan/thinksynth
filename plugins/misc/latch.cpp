/* $Id: latch.cpp,v 1.1 2003/10/29 03:11:05 ink Exp $ */

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

char		*desc = "Sample and hold";
thPluginState	mystate = thPassive;

extern "C" int	module_init (thPlugin *plugin);
extern "C" int	module_callback (thNode *node, thMod *mod, unsigned int windowlen);
extern "C" void module_cleanup (struct module *mod);

void module_cleanup (struct module *mod)
{
	printf("Latch plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("Latch plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	float *out, *out_last;
	thArg *in_arg, *in_latch, *inout_last;
	thArg *out_arg;
	unsigned int i;
	float sample;

	inout_last = mod->GetArg(node, "last");
	sample = (*inout_last)[0];
	out_last = inout_last->Allocate(1);

	in_arg = mod->GetArg(node, "in");
	in_latch = mod->GetArg(node, "latch");

	out_arg = mod->GetArg(node, "out");
	out = out_arg->Allocate(windowlen);

	for(i=0;i<windowlen;i++) {
		if((*in_latch)[i] > 0) {
			sample = (*in_arg)[i];
		}
		out[i] = sample;
	}

	out_last[0] = sample;

	return 0;
}
