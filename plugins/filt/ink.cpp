/* $Id: ink.cpp,v 1.2 2003/05/16 04:42:07 ink Exp $ */

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

#define SQR(x) (x*x)

char		*desc = "`INK Filter`  Gravity-based low pass";
thPluginState	mystate = thActive;

extern "C" int	module_init (thPlugin *plugin);
extern "C" int	module_callback (thNode *node, thMod *mod, unsigned int windowlen);
extern "C" void module_cleanup (struct module *mod);

void module_cleanup (struct module *mod)
{
	printf("inkfilt plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("inkfilt plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	float *out = new float[windowlen];
	float *out_accel = new float[windowlen];
	float *out_last = new float[1];
	float *out_last_accel = new float[1];
	thArgValue *in_arg, *in_cutoff, *in_res, *in_limit, *in_accel, *in_last;
	unsigned int i;
	float diff, accel;

	in_arg = (thArgValue *)mod->GetArg(node, "in");
	in_cutoff = (thArgValue *)mod->GetArg(node, "cutoff");
	in_res = (thArgValue *)mod->GetArg(node, "res");
	in_accel = (thArgValue *)mod->GetArg(node, "accel");
	in_last = (thArgValue *)mod->GetArg(node, "last");

	accel = (*in_accel)[0];
	*out_last = (*in_last)[0];

	for(i=0;i<windowlen;i++) {
		diff = (*in_arg)[i] - *out_last;

		accel += diff*SQR((*in_cutoff)[i]);
		accel *= 1-(1-SQR((*in_res)[i]));
		
		*out_last += accel;

		out_accel[i] = accel;

		out[i] = *out_last;
	}

	out_last_accel[0] = accel;

	node->SetArg("out", out, windowlen);
	node->SetArg("aout", out_accel, windowlen);
	node->SetArg("last", out_last, 1);
	node->SetArg("accel", out_last_accel, 1);

	return 0;
}

