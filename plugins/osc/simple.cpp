/* $Id: simple.cpp,v 1.18 2003/05/13 18:59:42 ink Exp $ */

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
	float *out = new float[windowlen];
	float *last = new float[1];
	float halfwave, ratio;
	float position, wavelength;
	thArgValue *in_freq, *in_pw, *in_waveform, *in_last;

	in_freq = (thArgValue *)mod->GetArg(node, "freq");
	in_pw = (thArgValue *)mod->GetArg(node, "pw");
	in_waveform = (thArgValue *)mod->GetArg(node, "waveform");
	in_last = (thArgValue *)mod->GetArg(node, "last");

	position = in_last->argValues[0];

	for(i=0; i < (int)windowlen; i++) {
		wavelength = TH_SAMPLE * (1.0/(*in_freq)[i]);
		position++;
		while(position > wavelength) {
			position -= wavelength;
		}
		
		ratio = position/wavelength;
		halfwave = wavelength/2;
		
		switch((int)(*in_waveform)[i]) {
			/* 0 = sine, 1 = sawtooth, 2 = square, 3 = tri, 4 = half-circle */
			case 0:    /* SINE WAVE */
				out[i] = TH_MAX*sin((ratio)*(2*M_PI)); /* This will fuck up if TH_MIX is not the negative of TH_MIN */
				break;
			case 1:    /* SAWTOOTH WAVE */
				out[i] = TH_RANGE*(ratio)+TH_MIN;
				break;
			case 2:    /* SQUARE WAVE */
				if(ratio < 0.5) {
					out[i] = TH_MIN;
				} else {
					out[i] = TH_MAX;
				}
				break;
			case 3:    /* TRIANGLE WAVE */
				if(position < halfwave) {
					out[i] = TH_RANGE*(position/halfwave)+TH_MIN;
				} else {
					out[i] = (-1*TH_RANGE)*((position-halfwave)/halfwave)+TH_MAX;
				}
				break;
			case 4:    /* HALF-CIRCLE WAVE */
				if(position<halfwave) {
					out[i] = 2*sqrt(2)*sqrt((wavelength-(2*position))*position/(wavelength*wavelength))*TH_MAX;
				} else {
					out[i] = 2*sqrt(2)*sqrt((wavelength-(2*(position-halfwave)))*(position-halfwave)/(wavelength*wavelength))*TH_MIN;
				}
		}
	}
	
	node->SetArg("out", out, windowlen);
	last[0] = position;
	node->SetArg("last", (float*)last, 1);
	
	return 0;
}
