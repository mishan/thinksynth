/* $Id: midi2freq.cpp,v 1.12 2004/04/09 08:14:33 ink Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

enum {IN_NOTE, OUT_ARG};
int args[OUT_ARG + 1];

char		*desc = "Converts a midi note value to it's respective frequency";
thPluginState	mystate = thPassive;

void module_cleanup (struct module *mod)
{
	printf("Midi2Freq plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("Midi2Freq plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	args[IN_NOTE] = plugin->RegArg("note");
	args[OUT_ARG] = plugin->RegArg("out");

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	float *out;
	thArg *in_note;
	thArg *out_arg;
	unsigned int i, argnum;

	in_note = mod->GetArg(node, args[IN_NOTE]);

	out_arg = mod->GetArg(node, args[OUT_ARG]);
	argnum = (unsigned int) in_note->argNum;
	out = out_arg->Allocate(argnum);

	for(i=0;i<argnum;i++) {
	  out[i] = 440*pow(2,((*in_note)[i]-69)/12);
	}

/*	node->SetArg("out", out, windowlen); */
	return 0;
}

