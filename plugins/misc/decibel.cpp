/* $Id: decibel.cpp,v 1.6 2004/04/08 00:34:56 misha Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

char		*desc = "Converts dB to an amplitude value. Arg should be <= 0.";
thPluginState	mystate = thPassive;

void module_cleanup (struct module *mod)
{
	printf("Decibel plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("Decibel plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	float *out;
	thArg *db;
	thArg *out_arg;
	unsigned int i, argnum;

	db = mod->GetArg(node, "db");

	out_arg = mod->GetArg(node, "out");
	argnum = (unsigned int)db->argNum;
	out = out_arg->Allocate(argnum);

	for(i=0;i<argnum;i++) {
		out[i] = exp((*db)[i]*.11512925f); // thx vorbis
	}

/*	node->SetArg("out", out, windowlen); */
	return 0;
}

