/* $Id: softsqr.cpp,v 1.20 2004/05/26 00:14:04 misha Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "think.h"

enum {IN_FREQ, IN_SFREQ, IN_PW, OUT_ARG, INOUT_LAST};
int args[INOUT_LAST + 1];

char		*desc = "Square wave with sine-like transitions";
thPluginState	mystate = thActive;

static const float M_PI2 = 2*M_PI;

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

	args[IN_FREQ] = plugin->RegArg("freq");
	args[IN_SFREQ] = plugin->RegArg("sfreq");
	args[IN_PW] = plugin->RegArg("pw");
	args[OUT_ARG] = plugin->RegArg("out");
	args[INOUT_LAST] = plugin->RegArg("last");
	
	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
{
	unsigned int i;

	float buf_freq[windowlen], buf_sfreq[windowlen], buf_pw[windowlen];

	float *out;
	float *out_last;
	float wavelength, sinewavelength, ratio;
	float sinewidth, minsqrwidth, maxsqrwidth, position, lendiff;
	int phase;
	thArg *in_freq, *in_pw, *in_sw;
	thArg *out_arg;
	thArg *inout_last;
	float val_freq, val_pw, val_sw;

	out_arg = mod->GetArg(node, args[OUT_ARG]);
	inout_last = mod->GetArg(node, args[INOUT_LAST]);

	position = (*inout_last)[0]; /* Where in the phase we are */
	phase = (int)(*inout_last)[1]; /* Which phase we are in */
	out_last = inout_last->Allocate(2);
	out = out_arg->Allocate(windowlen);

	in_freq = mod->GetArg(node, args[IN_FREQ]);
	in_sw = mod->GetArg(node, args[IN_SFREQ]); // Sine Freq
	in_pw = mod->GetArg(node, args[IN_PW]); // Pulse Width

	in_freq->GetBuffer(buf_freq, windowlen);
	in_pw->GetBuffer(buf_pw, windowlen);
	in_sw->GetBuffer(buf_sfreq, windowlen);

	/*  0 = sine from low-hi, 1 = high, 2 = hi-low, 3 = low  */
	for(i = 0; i < windowlen; i++) {
		val_freq = buf_freq[i];
		val_pw = buf_pw[i];
		val_sw = buf_sfreq[i];

		wavelength = samples * (1.0/val_freq);
		sinewavelength = samples * (1.0/val_sw);
		
		if(sinewavelength > wavelength) {
			sinewavelength = wavelength; /* otherwise the pitch bends when 
											sfreq is low */
		}

		lendiff = wavelength - sinewavelength;
		sinewidth = sinewavelength/2;

		maxsqrwidth = (lendiff) * val_pw;
		minsqrwidth = (lendiff) * (1-val_pw);

		//printf("SoftSQR  Freq: %f \tSineWidth %f\n", (*in_sw)[i], sinewidth);

		/* XXX: surely this math can be simplified ... */
		switch(phase) {
		case 0:    /* Sine segment from low to high */
			ratio = (position++)/sinewidth;
			ratio = (ratio * .5) + 0.75; /* We need the right part of the sine
										   wave */

			if(position >= sinewidth) { // End when its over
				position -= sinewidth;
				phase++;
			}
			out[i] = TH_MAX*sin(ratio*(M_PI2)); /* This will fuck up if TH_MIX
												   is not the negative of 
												   TH_MIN */
			break;
		case 1:    /* Maximum square */
			if(position++ > maxsqrwidth) {
				position -= maxsqrwidth;
				phase++;
			}
			out[i] = TH_MAX;
			break;
		case 2:    /* Sine segment from high to low */
			ratio = (position++)/sinewidth;
			ratio = (ratio * .5) + 0.25; /* We need the right part of the sine
										   wave */

			if(position >= sinewidth) {
				position -= sinewidth;
				phase++;
			}
			out[i] = TH_MAX*sin(ratio*(M_PI2)); /* This will fuck up if TH_MIX
												   is not the negative of
												   TH_MIN */
			break;
		case 3:    /* Minimum square */
			if(position++ > minsqrwidth) {
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
