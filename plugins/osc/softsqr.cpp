/*
 * Copyright (C) 2004-2014 Metaphonic Labs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "think.h"

enum {IN_FREQ, IN_SFREQ, IN_PW, OUT_ARG, INOUT_LAST};
int args[INOUT_LAST + 1];

char		*desc = "Square wave with sine-like transitions";
thPlugin::State	mystate = thPlugin::ACTIVE;

static const float M_PI2 = 2*M_PI;

void module_cleanup (struct module *mod)
{
}

/* ModuleLoad() invokes this function with a pointer to the plugin
 * instance. */
int module_init (thPlugin *plugin)
{
	plugin->setDesc (desc);
	plugin->setState (mystate);

	args[IN_FREQ] = plugin->regArg("freq");
	args[IN_SFREQ] = plugin->regArg("sfreq");
	args[IN_PW] = plugin->regArg("pw");
	args[OUT_ARG] = plugin->regArg("out");
	args[INOUT_LAST] = plugin->regArg("last");
	
	return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
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

	out_arg = mod->getArg(node, args[OUT_ARG]);
	inout_last = mod->getArg(node, args[INOUT_LAST]);

	position = (*inout_last)[0]; /* Where in the phase we are */
	phase = (int)(*inout_last)[1]; /* Which phase we are in */
	out_last = inout_last->allocate(2);
	out = out_arg->allocate(windowlen);

	in_freq = mod->getArg(node, args[IN_FREQ]);
	in_sw = mod->getArg(node, args[IN_SFREQ]); // Sine Freq
	in_pw = mod->getArg(node, args[IN_PW]); // Pulse Width

	in_freq->getBuffer(buf_freq, windowlen);
	in_pw->getBuffer(buf_pw, windowlen);
	in_sw->getBuffer(buf_sfreq, windowlen);

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
