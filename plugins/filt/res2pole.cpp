/* $Id: res2pole.cpp,v 1.1 2003/05/20 19:16:16 ink Exp $ */

/* Written by Leif Ames <ink@bespni.org>
   Algorithm taken from musicdsp.org
   References : Effect Deisgn Part 1, Jon Dattorro, J. Audio Eng. Soc., Vol 45, No. 9, 1997 September
*/

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

char		*desc = "Resonant 2-pole Chamberlin filter";
thPluginState	mystate = thActive;

extern "C" int	module_init (thPlugin *plugin);
extern "C" int	module_callback (thNode *node, thMod *mod, unsigned int windowlen);
extern "C" void module_cleanup (struct module *mod);

void module_cleanup (struct module *mod)
{
	printf("Chamberlin plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("Chamberlin plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	float *out, *highout, *bandout, *notchout, *last;
	thArgValue *in_arg, *in_cutoff, *in_res;
	thArgValue *out_low, *out_high, *out_band, *out_notch;
	thArgValue *inout_last;
	float f, q;
	unsigned int i;

	out_low = (thArgValue *)mod->GetArg(node, "out");
	out_high = (thArgValue *)mod->GetArg(node, "out_high");
	out_band = (thArgValue *)mod->GetArg(node, "out_band");
	out_notch = (thArgValue *)mod->GetArg(node, "out_notch");
	out = out_low->allocate(windowlen);
	highout = out_high->allocate(windowlen);
	bandout = out_band->allocate(windowlen);
	notchout = out_notch->allocate(windowlen);

	inout_last = (thArgValue *)mod->GetArg(node, "last");
	last = inout_last->allocate(4);

	in_arg = (thArgValue *)mod->GetArg(node, "in");
	in_cutoff = (thArgValue *)mod->GetArg(node, "cutoff");
	in_res = (thArgValue *)mod->GetArg(node, "res");

	for(i=0;i<windowlen;i++) {
		f = 2*sin(M_PI * (*in_cutoff)[i] / TH_SAMPLE);
		q = (*in_res)[i];

		last[0] += f * last[2];  /* Low Pass */
		last[1] = q * (*in_arg)[i] - last[0] - q * last[2]; /* High Pass */
		last[2] += f * last[1];
		last[3] = last[0] + last[1];
	
		out[i] = last[0];
		highout[i] = last[1];
		bandout[i] = last[2];
		notchout[i] = last[3];
	}

	return 0;
}

