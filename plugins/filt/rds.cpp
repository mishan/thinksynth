/* $Id: rds.cpp,v 1.8 2003/06/04 21:13:52 ink Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

#include "thArg.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thMod.h"
#include "thSynth.h"

#define SQR(a) ((a)*(a))

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
	float *out;
	float *highout;
	float *out_last;
	thArg *in_arg, *in_cutoff, *in_res;
	thArg *out_arg, *out_high;
	thArg *inout_last;
	unsigned int i;
	float last, diff;
	float prevdiff, rdiff;
	float fact, rfact;

	out_arg = mod->GetArg(node, "out");
	out_high = mod->GetArg(node, "out_high");
	inout_last = mod->GetArg(node, "last");

	last = (*inout_last)[0];
	prevdiff = (*inout_last)[1];
	out_last = inout_last->allocate(2);

	out = out_arg->allocate(windowlen);
	highout = out_high->allocate(windowlen);

	in_arg = mod->GetArg(node, "in");
	in_cutoff = mod->GetArg(node, "cutoff");
	in_res = mod->GetArg(node, "res");

	for(i=0;i<windowlen;i++) {
	  fact = (*in_cutoff)[i]; //1-(SQR((*in_cutoff)[i]));
	  rfact = 1-(SQR((*in_res)[i]));

	  diff = (*in_arg)[i] - last;
	  if(fabs(diff) > TH_RANGE) { /* in case the input is screwy */
		  diff = TH_RANGE * (diff > 0 ? 1 : -1);
	  }
	  diff *= 1-SQR((diff/(TH_RANGE+1))); /* My special blend of herbs and spices */

	  highout[i] = diff;
	  rdiff = diff - prevdiff;
	  rdiff *= rfact;
	  diff -= rdiff;
	  diff *= fact;
	  last += diff;

	  out[i] = last;
	}

	out_last[0] = last;
	out_last[1] = prevdiff;

	return 0;
}

