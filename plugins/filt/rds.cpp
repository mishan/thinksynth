/* $Id: rds.cpp,v 1.2 2003/05/08 01:49:35 ink Exp $ */

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

#define SQR(a) a*a

char		*desc = "Resonant Difference Scaling Filter";
thPluginState	mystate = thActive;

extern "C" int	module_init (thPlugin *plugin);
extern "C" int	module_callback (thNode *node, thMod *mod, unsigned int windowlen);
extern "C" void module_cleanup (struct module *mod);

void module_cleanup (struct module *mod)
{
	printf("RDS plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("RDS plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	float *out = new float[windowlen];
	float *highout = new float[windowlen];
	float *out_last = new float[1];
	float *out_prevdiff = new float[1];
	thArgValue *in_arg, *in_cutoff, *in_res;
	thArgValue *in_last, *in_prevdiff;
	unsigned int i;
	float last, diff;
	float prevdiff, rdiff;
	float fact, rfact;

	in_arg = (thArgValue *)mod->GetArg(node->GetName(), "in");
	in_cutoff = (thArgValue *)mod->GetArg(node->GetName(), "cutoff");
	in_res = (thArgValue *)mod->GetArg(node->GetName(), "res");
	in_last = (thArgValue *)mod->GetArg(node->GetName(), "last");
	in_prevdiff = (thArgValue *)mod->GetArg(node->GetName(), "prevdiff");

	prevdiff = (*in_prevdiff)[0];
	last = (*in_last)[0];

	for(i=0;i<windowlen;i++) {
	  fact = (*in_cutoff)[i]; //1-(SQR((*in_cutoff)[i]));
	  rfact = 1-(SQR((*in_res)[i]));

	  diff = (*in_arg)[i] - last;

	  highout[i] = diff;
	  rdiff = diff - prevdiff;
	  rdiff *= rfact;
	  diff -= rdiff;
	  diff *= fact;
	  last += diff;

	  out[i] = last;
	}

	out_last[0] = last;
	out_prevdiff[0] = prevdiff;

	node->SetArg("out", out, windowlen);
	node->SetArg("highout", highout, windowlen);
	node->SetArg("last", out_last, 1);
	node->SetArg("prevdiff", out_prevdiff, 1);

	return 0;
}

