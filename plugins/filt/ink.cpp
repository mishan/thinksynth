/* $Id: ink.cpp,v 1.7 2003/06/01 20:21:47 ink Exp $ */

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
	float *out;
	float *aout;
	float *out_last;
//	float *out_last_accel = new float[1];
	thArg *in_arg, *in_cutoff, *in_res;
	thArg *out_arg, *out_accel;
	thArg *inout_last;
	unsigned int i;
	float in, last, diff, accel;

	out_arg = mod->GetArg(node, "out");
	out_accel = mod->GetArg(node, "aout");
	inout_last = mod->GetArg(node, "last");

	last = (*inout_last)[0];
	accel = (*inout_last)[1];
	out_last = inout_last->allocate(2);

	out = out_arg->allocate(windowlen);
	aout = out_accel->allocate(windowlen);

	in_arg = mod->GetArg(node, "in");
	in_cutoff = mod->GetArg(node, "cutoff");
	in_res = mod->GetArg(node, "res");

	for(i=0;i<windowlen;i++) {
		in = (*in_arg)[i];
		diff = in - last;
		//printf("diff: %f \tperc: %f \tlast: %f \t%f\n\nres: %f \tcut: %f\n\n", diff, diff/TH_RANGE, last, accel, (*in_res)[i], (*in_cutoff)[i]);
		if(fabs(diff) > TH_RANGE) { /* damn unstable filters */
			diff = TH_RANGE * (diff > 0 ? 1 : -1);
		}
		diff *= 1-SQR((diff/(TH_RANGE+1))); /* My special blend of herbs and spices */
		accel += diff*SQR((*in_cutoff)[i]);
		accel *= (*in_res)[i];
		
		if(abs((int)accel) > TH_RANGE) { /* more instability protection */
			accel = 0;
			last = 0;
		}

		last += accel;

		aout[i] = accel;

		out[i] = last;
	}

	out_last[0] = last;
	out_last[1] = accel;

	return 0;
}

