/* $Id: ds.cpp,v 1.2 2003/06/11 05:27:20 ink Exp $ */

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

char		*desc = "Difference Scaling Filter";
thPluginState	mystate = thActive;

extern "C" int	module_init (thPlugin *plugin);
extern "C" int	module_callback (thNode *node, thMod *mod, unsigned int windowlen);
extern "C" void module_cleanup (struct module *mod);

void module_cleanup (struct module *mod)
{
	printf("DS plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("DS plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	float *out;
	float *highout;
	float *out_last;
	thArg *in_arg, *in_cutoff;
	thArg *out_arg, *out_high;
	thArg *inout_last;
	unsigned int i;
	float last, diff;
	float fact;

	out_arg = mod->GetArg(node, "out");
	out_high = mod->GetArg(node, "out_high");
	inout_last = mod->GetArg(node, "last");

	last = (*inout_last)[0];
	out_last = inout_last->allocate(1);

	out = out_arg->allocate(windowlen);
	highout = out_high->allocate(windowlen);

	in_arg = mod->GetArg(node, "in");
	in_cutoff = mod->GetArg(node, "cutoff");

	for(i=0;i<windowlen;i++) {
	  fact = (*in_cutoff)[i]; //1-(SQR((*in_cutoff)[i]));

	  diff = (*in_arg)[i] - last;
	  if(fabs(diff) > TH_RANGE) { /* in case the input is screwy */
		  diff = TH_RANGE * (diff > 0 ? 1 : -1);
	  }

	  highout[i] = diff/2;
	  diff *= fact;
	  last += diff;

	  out[i] = last;
	}

	out_last[0] = last;

	return 0;
}
