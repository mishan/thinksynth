/* $Id: midi2freq.cpp,v 1.11 2004/04/08 00:34:56 misha Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

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

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	float *out;
	thArg *in_note;
	thArg *out_arg;
	unsigned int i, argnum;

	in_note = mod->GetArg(node, "note");

	out_arg = mod->GetArg(node, "out");
	argnum = (unsigned int) in_note->argNum;
	out = out_arg->Allocate(argnum);

	for(i=0;i<argnum;i++) {
	  out[i] = 440*pow(2,((*in_note)[i]-69)/12);
	}

/*	node->SetArg("out", out, windowlen); */
	return 0;
}

