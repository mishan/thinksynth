/* $Id: dshape.cpp,v 1.2 2004/03/26 09:50:33 joshk Exp $ */

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

char		*desc = "Resonant Difference Shaping Filter";
thPluginState	mystate = thActive;





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
	thArg *in_arg, *in_cutoff, *in_res, *in_factor;
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
	out_last = inout_last->Allocate(2);

	out = out_arg->Allocate(windowlen);
	highout = out_high->Allocate(windowlen);

	in_arg = mod->GetArg(node, "in");
	in_cutoff = mod->GetArg(node, "cutoff");
	in_res = mod->GetArg(node, "res");
	in_factor = mod->GetArg(node, "factor");

	for(i=0;i<windowlen;i++) {
	  fact = (*in_cutoff)[i]; //1-(SQR((*in_cutoff)[i]));
	  rfact = 1-(SQR((*in_res)[i]));

	  diff = (*in_arg)[i] - last;
	  if(fabs(diff) > TH_RANGE) { /* in case the input is screwy */
		  diff = TH_RANGE * (diff > 0 ? 1 : -1);
	  }
	  diff *= pow(1-fabs(diff/(TH_RANGE+1)), (*in_factor)[i]); /* Diff shaping magic */

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

