/* $Id: window.cpp,v 1.2 2004/05/26 00:14:04 misha Exp $ */

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

static inline float SQR (float x)
{
	return x*x;
}

enum {IN_FREQ, IN_PW, IN_WAVEFORM, IN_RESET, OUT_ARG, OUT_SYNC,
	  OUT_SYNC2, INOUT_LAST};

int args[INOUT_LAST + 1];

char		*desc = "Complex Oscillator";
thPluginState	mystate = thActive;

const float SQRT2_2 = 2*sqrt(2);

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

	args[IN_FREQ] = plugin->RegArg("freq");
	args[IN_PW] = plugin->RegArg("pw");
	args[IN_WAVEFORM] = plugin->RegArg("waveform");
	args[IN_RESET] = plugin->RegArg("reset");

	args[OUT_ARG] = plugin->RegArg("out");
	args[OUT_SYNC] = plugin->RegArg("sync");
	args[OUT_SYNC2] = plugin->RegArg("sync2");

	args[INOUT_LAST] = plugin->RegArg("last");

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
{
	int i, halfway;
	float *out;
	float *out_last, *sync, *sync2;
	float halfwave, ratio;
	float position;
	double wavelength, freq;
	float pw; /* Make pw cooler! */
	thArg *in_freq, *in_pw, *in_waveform, *in_reset;
	thArg *out_arg, *out_sync, *out_sync2;
	thArg *inout_last;

	out_arg = mod->GetArg(node, args[OUT_ARG]);
	out_sync = mod->GetArg(node, args[OUT_SYNC]); /* Output a 1 when the wave begins 
											 its cycle */
	out_sync2 = mod->GetArg(node, args[OUT_SYNC2]);
	inout_last = mod->GetArg(node, args[INOUT_LAST]);
	position = (*inout_last)[0];
	out_last = inout_last->Allocate(2);
	sync = out_sync->Allocate(windowlen);
	sync2 = out_sync2->Allocate(windowlen);
	out = out_arg->Allocate(windowlen);
	in_freq = mod->GetArg(node, args[IN_FREQ]);
	in_pw = mod->GetArg(node, args[IN_PW]);
	in_waveform = mod->GetArg(node, args[IN_WAVEFORM]);

	in_reset = mod->GetArg(node, args[IN_RESET]); /* Reset position to 0 when this 
											  goes to 1 */

	halfway = (int)(*inout_last)[1];

	for(i=0; i < (int)windowlen; i++) {
		freq = (*in_freq)[i];

		wavelength = samples/freq;

		if(position > wavelength || (*in_reset)[i] == 1) {
			position -= wavelength;
		}

		sync[i] = 0;
		sync2[i] = 0;

		pw = (*in_pw)[i];  /* Pulse Width */
		if(pw == 0) {
			pw = 0.5;
		}

		halfwave = wavelength * pw;
		if(position < halfwave) {
			if(halfway) {
				halfway = 0;
				sync[i] = 1;
			}
			ratio = position/(2*halfwave);
		} else {
			if(!halfway)
			{
				halfway = 1;
				sync2[i] = 1;
			}
			ratio = 0.5*((position - halfwave) / (wavelength - halfwave)) + 0.5;
		}

		switch((int)(*in_waveform)[i]) {
			/* 0 = sine, 1 = sawtooth, 2 = square, 3 = tri, 4 = half-circle,
			   5 = parabola */
			case 0:    /* SINE WAVE */
				out[i] = (sin((ratio-.25)*2*M_PI) + 1)/2;
				break;
//			case 1:    /* SAWTOOTH WAVE */
//				out[i] = amp_range*ratio+amp_min;
//				break;
			case 2:    /* SQUARE WAVE */
			{
				if(ratio <= 0.5)
					out[i] = 0;
				else
					out[i] = 1;
				break;
			}
			case 3:    /* TRIANGLE WAVE */
				ratio *= 2;
				if(ratio < 1) {
					out[i] = ratio;
				} else {
					out[i] = (-ratio + 2);
				}
				break;
//			case 4:    /* HALF-CIRCLE WAVE */
/*				if(ratio < 0.5) {
					out[i] = SQRT2_2*sqrt((wavelength-(2*position))*position/
										  SQR(wavelength))*amp_max;
				} else {
					out[i] = SQRT2_2*sqrt((wavelength-(2*(position-halfwave)))
										  *(position-halfwave)/SQR(wavelength))
						*amp_min;
				}
				break;
*/
//			case 5:    /* PARABOLA WAVE */
/*				if(ratio < 0.5) {
					out[i] = amp_max*(1-SQR(ratio*4-1));
				} else {
					out[i] = amp_max*(SQR((ratio-0.5)*4-1)-1);
				}
*/
		}
		position++;
	}
	
	out_last[0] = position;
	out_last[1] = halfway;

	return 0;
}
