/* $Id: softsqr.cpp,v 1.11 2003/05/17 16:01:22 ink Exp $ */

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

char		*desc = "Square wave with sine-like transitions";
thPluginState	mystate = thActive;

extern "C" int	module_init (thPlugin *plugin);
extern "C" int	module_callback (thNode *node, thMod *mod, unsigned int windowlen);
extern "C" void module_cleanup (struct module *mod);

void module_cleanup (struct module *mod)
{
	printf("SoftSQR module unloading\n");
}

/* ModuleLoad() invokes this function with a pointer to the plugin
 * instance. */
int module_init (thPlugin *plugin)
{
	printf("SoftSQR plugin loaded\n");
	plugin->SetDesc (desc);
	plugin->SetState (mystate);
	
	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	int i;
	float *out;
	float *out_last;
	float wavelength, sinewavelength, ratio;
	float sinewidth, minsqrwidth, maxsqrwidth, position;
	int phase;
	thArgValue *in_freq, *in_pw, *in_sw;
	thArgValue *out_arg;
	thArgValue *inout_last;

	out_arg = (thArgValue *)mod->GetArg(node, "out");
	inout_last = (thArgValue *)mod->GetArg(node, "last");

	position = (*inout_last)[0]; /* Where in the phase we are */
	phase = (int)(*inout_last)[1]; /* Which phase we are in */
	out_last = inout_last->allocate(2);
	out = out_arg->allocate(windowlen);

	in_freq = (thArgValue *)mod->GetArg(node, "freq");
	in_pw = (thArgValue *)mod->GetArg(node, "pw"); // Pulse Width
	in_sw = (thArgValue *)mod->GetArg(node, "sfreq"); // Sine Freq

	/*  0 = sine from low-hi, 1 = high, 2 = hi-low, 3 = low  */

	for(i=0; i < (int)windowlen; i++) {
		wavelength = TH_SAMPLE * (1.0/(*in_freq)[i]);
		sinewavelength = TH_SAMPLE * (1.0/(*in_sw)[i]);
		
		if(sinewavelength > wavelength) {
			sinewavelength = wavelength; /* otherwise the pitch bends when sfreq is low */
		}

		sinewidth = sinewavelength/2;
		maxsqrwidth = (wavelength - sinewavelength) * (*in_pw)[i];
		minsqrwidth = (wavelength - sinewavelength) * (1-(*in_pw)[i]);
		//printf("SoftSQR  Freq: %f \tSineWidth %f\n", (*in_sw)[i], sinewidth);
		switch(phase) {
		case 0:    /* Sine segment from low to high */
			ratio = position++/sinewidth;
			ratio = (ratio/2)+0.75; // We need the right part of the sine wave
			if(position >= sinewidth) { // End when its over
				position -= sinewidth;
				phase++;
			}
			out[i] = TH_MAX*sin(ratio*(2*M_PI)); /* This will fuck up if TH_MIX is not the negative of TH_MIN */
			break;
		case 1:    /* Maximum square */
			if(position++>maxsqrwidth) {
				position -= maxsqrwidth;
				phase++;
			}
			out[i] = TH_MAX;
			break;
		case 2:    /* Sine segment from high to low */
			ratio = position++/sinewidth;
			ratio = (ratio/2)+0.25; // We need the right part of the sine wave
			if(position >= sinewidth) {
				position -= sinewidth;
				phase++;
			}
			out[i] = TH_MAX*sin(ratio*(2*M_PI)); /* This will fuck up if TH_MIX is not the negative of TH_MIN */
			break;
		case 3:    /* Minimum square */
			if(position++>minsqrwidth) {
				position -= minsqrwidth;
				phase = 0;
			}
			out[i] = TH_MIN;
			break;
		}
	}
	
/*	node->SetArg("out", out, windowlen); */
	out_last[0] = position;
	out_last[1] = (float)phase;
/*	node->SetArg("position", (float*)last, 2); */
	
	return 0;
}
