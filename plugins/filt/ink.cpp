/* $Id: ink.cpp,v 1.13 2004/05/08 19:39:40 ink Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

#define SQR(x) (x*x)

enum {IN_ARG, IN_CUTOFF, IN_RES, OUT_ARG, OUT_AOUT, INOUT_LAST};
int args[INOUT_LAST + 1];

char		*desc = "`INK Filter`  Gravity-based low pass";
thPluginState	mystate = thActive;

void module_cleanup (struct module *mod)
{
	printf("inkfilt plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("inkfilt plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	args[IN_ARG] = plugin->RegArg("in");
	args[IN_CUTOFF] = plugin->RegArg("cutoff");
	args[IN_RES] = plugin->RegArg("res");

	args[OUT_ARG] = plugin->RegArg("out");
	args[OUT_AOUT] = plugin->RegArg("aout");

	args[INOUT_LAST] = plugin->RegArg("last");

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
	float buf_in[windowlen], buf_cut[windowlen], buf_res[windowlen];
	unsigned int i;
	float in, last, diff, accel;
	float val_arg, val_cutoff, val_res;

	out_arg = mod->GetArg(node, args[OUT_ARG]);
	out_accel = mod->GetArg(node, args[OUT_AOUT]);
	inout_last = mod->GetArg(node, args[INOUT_LAST]);

	last = (*inout_last)[0];
	accel = (*inout_last)[1];
	out_last = inout_last->Allocate(2);

	out = out_arg->Allocate(windowlen);
	aout = out_accel->Allocate(windowlen);

	in_arg = mod->GetArg(node, args[IN_ARG]);
	in_cutoff = mod->GetArg(node, args[IN_CUTOFF]);
	in_res = mod->GetArg(node, args[IN_RES]);
	
	in_arg->GetBuffer(buf_in, windowlen);
	in_cutoff->GetBuffer(buf_cut, windowlen);
	in_res->GetBuffer(buf_res, windowlen);

	for(i = 0; i < windowlen; i++) {
		val_arg = buf_in[i];
		val_cutoff = buf_cut[i];
		val_res = buf_res[i];

		in = val_arg;
		diff = in - last;
		//printf("diff: %f \tperc: %f \tlast: %f \t%f\n\nres: %f \tcut: %f\n\n", diff, diff/TH_RANGE, last, accel, (*in_res)[i], (*in_cutoff)[i]);
		if(fabs(diff) > TH_RANGE) { /* damn unstable filters */
			diff = TH_RANGE * (diff > 0 ? 1 : -1);
		}
		diff *= 1-SQR((diff/(TH_RANGE+1))); /* My special blend of herbs and 
											   spices */
		accel += diff*SQR(val_cutoff);
		accel *= val_res;
		
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

