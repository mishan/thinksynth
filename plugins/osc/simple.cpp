/* $Id: simple.cpp,v 1.26 2003/05/22 08:50:45 ink Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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

char		*desc = "Basic Oscillator";
thPluginState	mystate = thActive;

extern "C" int	module_init (thPlugin *plugin);
extern "C" int	module_callback (thNode *node, thMod *mod, unsigned int windowlen);
extern "C" void module_cleanup (struct module *mod);

void module_cleanup (struct module *mod)
{
	printf("Oscillator module unloading\n");
}

/* ModuleLoad() invokes this function with a pointer to the plugin
 * instance. */
int module_init (thPlugin *plugin)
{
	printf("Simple Oscillator plugin loaded\n");
	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	int i;
	float *out;
	float *out_last;
	float halfwave, ratio;
	float position, wavelength;
	float pw; /* Make pw cooler! */
	float fmamt;
	thArgValue *in_freq, *in_pw, *in_waveform, *in_fm, *in_fmamt;
	thArgValue *out_arg;
	thArgValue *inout_last;

	out_arg = (thArgValue *)mod->GetArg(node, "out");
	inout_last = (thArgValue *)mod->GetArg(node, "last");
	position = (*inout_last)[0];
	out_last = inout_last->allocate(1);

	out = out_arg->allocate(windowlen);

	in_freq = (thArgValue *)mod->GetArg(node, "freq");
	in_pw = (thArgValue *)mod->GetArg(node, "pw");
	in_waveform = (thArgValue *)mod->GetArg(node, "waveform");
	in_fm = (thArgValue *)mod->GetArg(node, "fm"); /* FM Input */
	in_fmamt = (thArgValue *)mod->GetArg(node, "fmamt"); /* Modulation amount */

	for(i=0; i < (int)windowlen; i++) {
		wavelength = TH_SAMPLE/(*in_freq)[i];
		position++;

		fmamt = (*in_fmamt)[i]; /* If FM is being used, apply it! */
		if(fmamt) {
			wavelength += (wavelength * fmamt) * ((*in_fm)[i] / TH_MAX);
		}
		if(position > wavelength) {
			position = 0;
		}

		pw = (*in_pw)[i];  /* Pulse Width */
		if(pw == 0) {
			pw = 0.5;
		}

		halfwave = wavelength * pw;
		if(position < halfwave) {
			ratio = position/(2*halfwave);
		} else {
			ratio = (((position-halfwave)/(wavelength-halfwave))/2)+0.5;
		}

		switch((int)(*in_waveform)[i]) {
			/* 0 = sine, 1 = sawtooth, 2 = square, 3 = tri, 4 = half-circle */
		case 0:    /* SINE WAVE */
			out[i] = TH_MAX*sin(ratio*2*M_PI); /* This will fuck up if TH_MIX is not the negative of TH_MIN */
			break;
		case 1:    /* SAWTOOTH WAVE */
			out[i] = TH_RANGE*ratio+TH_MIN;
			break;
		case 2:    /* SQUARE WAVE */
			if(ratio < 0.5) {
				out[i] = TH_MIN;
			} else {
				out[i] = TH_MAX;
			}
			break;
		case 3:    /* TRIANGLE WAVE */
			ratio *= 2;
			if(ratio < 1) {
				out[i] = TH_RANGE*ratio+TH_MIN;
			} else {
				out[i] = (-TH_RANGE)*(ratio-1)+TH_MAX;
			}
			break;
		case 4:    /* HALF-CIRCLE WAVE */
			if(ratio < 0.5) {
				out[i] = 2*sqrt(2)*sqrt((wavelength-(2*position))*position/(wavelength*wavelength))*TH_MAX;
			} else {
				out[i] = 2*sqrt(2)*sqrt((wavelength-(2*(position-halfwave)))*(position-halfwave)/(wavelength*wavelength))*TH_MIN;
			}
		}
	}

	out_last[0] = position;
/*	node->SetArg("out", out, windowlen);
	last[0] = position;
	node->SetArg("last", (float*)last, 1);
*/	
	return 0;
}
