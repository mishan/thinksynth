/* $Id: multisined.cpp,v 1.5 2004/05/26 00:14:04 misha Exp $ */

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
	printf("Multiwave module unloading\n");
}

/* ModuleLoad() invokes this function with a pointer to the plugin
 * instance. */
int module_init (thPlugin *plugin)
{
	printf("Multiwave plugin loaded\n");
	plugin->SetDesc (desc);
	plugin->SetState (mystate);

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
	
	in_waves = mod->GetArg(node, "waves");
	waves = (int)(*in_waves)[0];
	
	out_arg = mod->GetArg(node, "out");
	out_sync = mod->GetArg(node, "sync"); /* Output a 1 when the wave begins 
											 its cycle */
	inout_last = mod->GetArg(node, "last");
	position = (*inout_last)[0];
	out_last = inout_last->Allocate(waves);

	inout_freq = mod->GetArg(node, "freqbuffer");
	out_freq = inout_freq->Allocate(waves);

	sync = out_sync->Allocate(windowlen);

	out = out_arg->Allocate(windowlen);

	in_freq = mod->GetArg(node, "freq");
	in_amp = mod->GetArg(node, "amp");
	in_detunefreq = mod->GetArg(node, "detunefreq");
	in_detuneamt = mod->GetArg(node, "detuneamt");
	in_ampmul = mod->GetArg(node, "ampmul");
	in_ampadd = mod->GetArg(node, "ampadd");

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
