/* $Id$ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

#define SQR(a) ((a)*(a))

char		*desc = "Resonant Difference Scaling Filter";
thPluginState	mystate = thActive;

void module_cleanup (struct module *mod)
{
}

enum { OUT_ARG,OUT_HIGH,INOUT_LAST,IN_ARG,IN_CUTOFF,IN_RES };

int args[IN_RES + 1];

int module_init (thPlugin *plugin)
{
	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	args[OUT_ARG] = plugin->RegArg("out");
	args[OUT_HIGH] = plugin->RegArg("out_high");
	args[INOUT_LAST] = plugin->RegArg("last");
	args[IN_ARG] = plugin->RegArg("in");
	args[IN_CUTOFF] = plugin->RegArg("cutoff");
	args[IN_RES] = plugin->RegArg("res");
	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
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

	out_arg = mod->GetArg(node, args[OUT_ARG]);
	out_high = mod->GetArg(node, args[OUT_HIGH]);
	inout_last = mod->GetArg(node, args[INOUT_LAST]);

	last = (*inout_last)[0];
	prevdiff = (*inout_last)[1];
	out_last = inout_last->Allocate(2);

	out = out_arg->Allocate(windowlen);
	highout = out_high->Allocate(windowlen);

	in_arg = mod->GetArg(node, args[IN_ARG]);
	in_cutoff = mod->GetArg(node, args[IN_CUTOFF]);
	in_res = mod->GetArg(node, args[IN_RES]);

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
