/* $Id: resgrav.cpp,v 1.1 2003/06/01 18:47:48 ink Exp $ */

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

#define SQR(x) ((x)*(x))

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
	thArg *in_arg, *in_cutoff, *in_res;
	thArg *out_arg, *out_accel;
	thArg *inout_last;
	unsigned int i;
	float in, last, accel;

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
		if(last > in) {
			accel -= (*in_cutoff)[i];
		}
		else if (last < in) {
			accel += (*in_cutoff)[i];
		}

		accel *= 1+(-1*SQR((*in_res)[i]-1));
//		printf("%f \t%f\n", (*in_res)[i], 1+(-1*SQR((*in_res)[i]-1)));
		last += accel;

		aout[i] = accel;

		out[i] = last;
	}

	out_last[0] = last;
	out_last[1] = accel;

	return 0;
}

