/* $Id$ */
/*
 * Copyright (C) 2004 Metaphonic Labs
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

enum {IN_FREQ, IN_BAND, IN_PW, OUT_ARG, INOUT_LAST};
int args[INOUT_LAST + 1];

char		*desc = "Band-pass oscillator";
thPlugin::State	mystate = thPlugin::ACTIVE;

void module_cleanup (struct module *mod)
{
}

/* thPlugin::moduleLoad() invokes this function with a pointer to the plugin
 * instance. */
int module_init (thPlugin *plugin)
{
	plugin->setDesc (desc);
	plugin->setState (mystate);
	
	args[IN_FREQ] = plugin->regArg("freq");
	args[IN_BAND] = plugin->regArg("band");
	args[IN_PW] = plugin->regArg("pw");
	args[OUT_ARG] = plugin->regArg("out");
	args[INOUT_LAST] = plugin->regArg("last");

	return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
					 unsigned int samples)
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

	out_arg = mod->getArg(node, args[OUT_ARG]);
	inout_last = mod->getArg(node, args[INOUT_LAST]);

	position = (*inout_last)[0]; /* Where in the phase we are */
	phase = (int)(*inout_last)[1]; /* Which phase we are in */
	out_last = inout_last->allocate(2);
	out = out_arg->allocate(windowlen);

	in_freq = mod->getArg(node, args[IN_FREQ]);
	in_band = mod->getArg(node, args[IN_BAND]); // Band Freq
	in_pw = mod->getArg(node, args[IN_PW]); // Pulse Width

	/*  0 = top sine, 1 = first 0, 2 = bottom sine, 3 = second 0  */

	for(i = 0; i < (int)windowlen; i++)
	{
		wavelength = samples * (1.0/(*in_freq)[i]);
		sinewavelength = samples * (1.0/(*in_band)[i]);
		
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
