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

#define SQR(x) ((x)*(x))

char		*desc = "Multiple Parabola Waves";
thPluginState	mystate = thActive;

void module_cleanup (struct module *mod)
{
}

/* ModuleLoad() invokes this function with a pointer to the plugin
 * instance. */
enum { IN_WAVES,OUT_ARG,OUT_SYNC,INOUT_LAST,INOUT_FREQ,IN_FREQ,IN_AMP,IN_DETUNEFREQ,IN_DETUNEAMT,IN_AMPMUL,IN_AMPADD };

int args[IN_AMPADD + 1];

int module_init (thPlugin *plugin)
{
	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	args[IN_WAVES] = plugin->RegArg("waves");
	args[OUT_ARG] = plugin->RegArg("out");
	args[OUT_SYNC] = plugin->RegArg("sync");
	args[INOUT_LAST] = plugin->RegArg("last");
	args[INOUT_FREQ] = plugin->RegArg("freqbuffer");
	args[IN_FREQ] = plugin->RegArg("freq");
	args[IN_AMP] = plugin->RegArg("amp");
	args[IN_DETUNEFREQ] = plugin->RegArg("detunefreq");
	args[IN_DETUNEAMT] = plugin->RegArg("detuneamt");
	args[IN_AMPMUL] = plugin->RegArg("ampmul");
	args[IN_AMPADD] = plugin->RegArg("ampadd");
	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
{
	int i, j;
	float *out;
	float *out_last, *sync, *out_freq;
	float position, wfreq;
	double wavelength, freq;
	float amp_max, amp_min, amp_range;
	float detunefreq, detuneamt, ampmul, ampadd;
	int waves;
	thArg *in_freq, *in_amp, *in_waves, *in_detunefreq, *in_detuneamt, *in_ampmul, *in_ampadd;
	thArg *out_arg, *out_sync;
	thArg *inout_last, *inout_freq;
	
	in_waves = mod->GetArg(node, args[IN_WAVES]);
	waves = (int)(*in_waves)[0];
	
	out_arg = mod->GetArg(node, args[OUT_ARG]);
	out_sync = mod->GetArg(node, args[OUT_SYNC]);
	/* Output a 1 when the wave begins its cycle */
	inout_last = mod->GetArg(node, args[INOUT_LAST]);
	position = (*inout_last)[0];
	out_last = inout_last->Allocate(waves);

	inout_freq = mod->GetArg(node, args[INOUT_FREQ]);
	out_freq = inout_freq->Allocate(waves);

	sync = out_sync->Allocate(windowlen);

	out = out_arg->Allocate(windowlen);

	in_freq = mod->GetArg(node, args[IN_FREQ]);
	in_amp = mod->GetArg(node, args[IN_AMP]);
	in_detunefreq = mod->GetArg(node, args[IN_DETUNEFREQ]);
	in_detuneamt = mod->GetArg(node, args[IN_DETUNEAMT]);
	in_ampmul = mod->GetArg(node, args[IN_AMPMUL]);
	in_ampadd = mod->GetArg(node, args[IN_AMPADD]);

	for(i = 0; i < (int)windowlen; i++) {
		//wavelength = samples/(*in_freq)[i];
		freq = (*in_freq)[i];
		amp_max = (*in_amp)[i];
		if(amp_max == 0) {
		  amp_max = TH_MAX;
		}
		amp_min = -amp_max;
		amp_range = amp_max-amp_min;

		detunefreq = (*in_detunefreq)[i];
		detuneamt = (*in_detuneamt)[i];
		ampmul = (*in_ampmul)[i];
		ampadd = (*in_ampadd)[i];

		wavelength = samples / freq;
		out[i] = sin(2 * M_PI * out_last[0]/wavelength) * amp_max;
		if(++(out_last[0]) > wavelength)
			out_last[0] = 0;

		for(j = 1; j < waves; j++)
		{
			wfreq = freq + sin(j * (detunefreq / (2 * M_PI))) * detuneamt * freq;
			wavelength = samples / wfreq;
			out[i] += ((out_last[j] / wavelength) - .5) * 2 * (amp_max * (pow(ampmul, j) + (ampadd * j)));
//			out[i] += sin(2 * M_PI * out_last[j] / wavelength) * (amp_max * (pow(ampmul, j) + (ampadd * j)));
			if(++(out_last[j]) > wavelength)
				out_last[j] = 0;
		}
	}
/*	node->SetArg("out", out, windowlen);
	last[0] = position;
	node->SetArg("last", (float*)last, 1);
*/	
	return 0;
}
