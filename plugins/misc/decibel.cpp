/* $Id: decibel.cpp,v 1.1 2003/05/24 08:44:58 aaronl Exp $ */

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

char		*desc = "Converts dB to an amplitude value. Arg should be <= 0.";
thPluginState	mystate = thPassive;

extern "C" int	module_init (thPlugin *plugin);
extern "C" int	module_callback (thNode *node, thMod *mod, unsigned int windowlen);
extern "C" void module_cleanup (struct module *mod);

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
	thArgValue *db;
	thArgValue *out_arg;
	unsigned int i, argnum;

	db = (thArgValue *)mod->GetArg(node, "db");

	out_arg = (thArgValue *)mod->GetArg(node, "out");
	argnum = (unsigned int)db->argNum;
	out = out_arg->allocate(argnum);

	for(i=0;i<argnum;i++) {
		out[i] = pow(10,(*db)[i]*.05);
	}

/*	node->SetArg("out", out, windowlen); */
	return 0;
}

