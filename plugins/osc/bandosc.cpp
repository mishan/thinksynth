/* $Id: bandosc.cpp,v 1.1 2003/06/17 01:00:43 ink Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "think.h"

#include "thArg.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thMod.h"
#include "thSynth.h"

char		*desc = "Band-pass oscillator";
thPluginState	mystate = thActive;

extern "C" int	module_init (thPlugin *plugin);
extern "C" int	module_callback (thNode *node, thMod *mod, unsigned int windowlen);
extern "C" void module_cleanup (struct module *mod);

void module_cleanup (struct module *mod)
{
	printf("BandOsc module unloading\n");
}

/* ModuleLoad() invokes this function with a pointer to the plugin
 * instance. */
int module_init (thPlugin *plugin)
{
	printf("BandOsc plugin loaded\n");
	plugin->SetDesc (desc);
	plugin->SetState (mystate);
	
	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	int i;
	float *out;
	float *out_last;
	float wavelength, sinewavelength, ratio, pw;
	float sinewidth, minsqrwidth, maxsqrwidth, position;
	int phase;
	thArg *in_freq, *in_band, *in_pw;
	thArg *out_arg;
	thArg *inout_last;

	out_arg = mod->GetArg(node, "out");
	inout_last = mod->GetArg(node, "last");

	position = (*inout_last)[0]; /* Where in the phase we are */
	phase = (int)(*inout_last)[1]; /* Which phase we are in */
	out_last = inout_last->allocate(2);
	out = out_arg->allocate(windowlen);

	in_freq = mod->GetArg(node, "freq");
	in_band = mod->GetArg(node, "band"); // Band Freq
	in_pw = mod->GetArg(node, "pw"); // Pulse Width

	/*  0 = top sine, 1 = first 0, 2 = bottom sine, 3 = second 0  */

	for(i=0; i < (int)windowlen; i++) {
		wavelength = TH_SAMPLE * (1.0/(*in_freq)[i]);
		sinewavelength = TH_SAMPLE * (1.0/(*in_band)[i]);
		
		if(sinewavelength > wavelength) {
			sinewavelength = wavelength; /* otherwise the pitch bends when band is low */
		}

		sinewidth = sinewavelength/2;
		pw = (*in_pw)[i];
		maxsqrwidth = (wavelength - sinewavelength) * pw;
		minsqrwidth = (wavelength - sinewavelength) * (1-pw);
		switch(phase) {
		case 0:    /* Positive sine */
			ratio = position++/sinewidth;
			ratio += 0.75; // We need the right part of the sine wave
			if(position >= sinewidth) { // End when its over
				position -= sinewidth;
				phase++;
			}
			out[i] = TH_MAX*(sin(ratio*(2*M_PI))/2 + 0.5); /* This will fuck up if TH_MIX is not the negative of TH_MIN */
			break;
		case 1:    /* First 0 segment */
			if(position++>maxsqrwidth) {
				position -= maxsqrwidth;
				phase++;
			}
			out[i] = 0;
			break;
		case 2:    /* Negative sine */
			ratio = position++/sinewidth;
			ratio += 0.25; // We need the right part of the sine wave
			if(position >= sinewidth) {
				position -= sinewidth;
				phase++;
			}
			out[i] = TH_MAX*(sin(ratio*(2*M_PI))/2 - 0.5); /* This will fuck up if TH_MIX is not the negative of TH_MIN */
			break;
		case 3:    /* Second 0 segment */
			if(position++>minsqrwidth) {
				position -= minsqrwidth;
				phase = 0;
			}
			out[i] = 0;
			break;
		}
	}
	
	out_last[0] = position;
	out_last[1] = (float)phase;
	
	return 0;
}
