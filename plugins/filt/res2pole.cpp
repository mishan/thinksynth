/* $Id: res2pole.cpp,v 1.4 2003/05/30 00:55:41 aaronl Exp $ */

/* Written by Leif Ames <ink@bespni.org>
   Algorithm taken from musicdsp.org
   References : Hal Chamberlin, "Musical Applications of Microprocessors," 2nd Ed, Hayden Book Company 1985. pp 490-492
*/

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
	float *out, *highout, *bandout, *notchout, *delay;
	thArg *in_arg, *in_cutoff, *in_res;
	thArg *out_low, *out_high, *out_band, *out_notch;
	thArg *inout_delay;
	float f, q;
	unsigned int i;

	out_low = mod->GetArg(node, "out");
	out_high = mod->GetArg(node, "out_high");
	out_band = mod->GetArg(node, "out_band");
	out_notch = mod->GetArg(node, "out_notch");
	out = out_low->allocate(windowlen);
	highout = out_high->allocate(windowlen);
	bandout = out_band->allocate(windowlen);
	notchout = out_notch->allocate(windowlen);

	inout_delay = mod->GetArg(node, "delay");
	delay = inout_delay->allocate(2);

	in_arg = mod->GetArg(node, "in");
	in_cutoff = mod->GetArg(node, "cutoff");
	in_res = mod->GetArg(node, "res");

	for(i=0;i<windowlen;i++) {
		f = 2*sin(M_PI * (*in_cutoff)[i] / TH_SAMPLE);
		q = 1/((*in_res)[i]*2);

		out[i] = delay[1] + f * delay[0];  /* Low Pass */
		highout[i] = (*in_arg)[i] - out[i] - q * delay[0]; /* High Pass */
		bandout[i] = f * highout[i] + delay[0];
		notchout[i] = highout[i] + out[i];
	
		delay[0] = bandout[i];
		delay[1] = out[i];
	}

	return 0;
}

