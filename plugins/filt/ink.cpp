/* $Id: ink.cpp,v 1.4 2003/05/30 00:55:41 aaronl Exp $ */

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
	float last, diff, accel;

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
		diff = (*in_arg)[i] - last;

		accel += diff*SQR((*in_cutoff)[i]);
		accel *= 1-(1-SQR((*in_res)[i]));
		
		last += accel;

		aout[i] = accel;

		out[i] = last;
	}

	out_last[0] = last;
	out_last[1] = accel;
/*	out_last_accel[0] = accel;

	node->SetArg("out", out, windowlen);
	node->SetArg("aout", out_accel, windowlen);
	node->SetArg("last", out_last, 1);
	node->SetArg("accel", out_last_accel, 1);
*/
	return 0;
}

