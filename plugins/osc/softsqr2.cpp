/* $Id: softsqr2.cpp,v 1.9 2004/03/26 09:38:37 joshk Exp $ */

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

char		*desc = "Square wave with sine-like transitions, proportional to the frequency";
thPluginState	mystate = thActive;

extern "C" {
  int	module_init (thPlugin *plugin);
  int	module_callback (thNode *node, thMod *mod, unsigned int windowlen);
  void module_cleanup (struct module *mod);
}

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
	float wavelength, ratio;
	float sinewidth, minsqrwidth, maxsqrwidth;
	float position;
	int phase;
	thArg *in_freq, *in_pw, *in_sw;
	thArg *out_arg;
	thArg *inout_last;

	out_arg = mod->GetArg(node, "out");
	inout_last = mod->GetArg(node, "last");
	position = (*inout_last)[0];
	phase = (int)(*inout_last)[1];
	out_last = inout_last->Allocate(2);
	out = out_arg->Allocate(windowlen);

	in_freq = mod->GetArg(node, "freq");
	in_pw = mod->GetArg(node, "pw"); // Pulse Width
	in_sw = mod->GetArg(node, "sw"); // Sine Width

	/*  0 = sine from low-hi, 1 = high, 2 = hi-low, 3 = low  */

	for(i=0; i < (int)windowlen; i++) {
		wavelength = TH_SAMPLE * (1.0/(*in_freq)[i]);
		
		sinewidth = wavelength * (*in_sw)[i];
		maxsqrwidth = (wavelength - (2*sinewidth)) * (*in_pw)[i];
		minsqrwidth = (wavelength - (2*sinewidth)) * (1-(*in_pw)[i]);
		switch(phase) {
		case 0:    /* Sine segment from low to high */
			ratio = position++/sinewidth;
			ratio = (ratio/2)+0.75; // We need the right part of the sine wave
			if(position >= sinewidth) { // End when its over
				position = 0;
				phase++;
			}
			out[i] = TH_MAX*sin(ratio*(2*M_PI)); /* This will fuck up if TH_MIX is not the negative of TH_MIN */
			break;
		case 1:    /* Maximum square */
			if(position++>maxsqrwidth) {
				position = 0;
				phase++;
			}
			out[i] = TH_MAX;
			break;
		case 2:    /* Sine segment from high to low */
			ratio = position++/sinewidth;
			ratio = (ratio/2)+0.25; // We need the right part of the sine wave
			if(position >= sinewidth) {
				position = 0;
				phase++;
			}
			out[i] = TH_MAX*sin(ratio*(2*M_PI)); /* This will fuck up if TH_MIX is not the negative of TH_MIN */
			break;
		case 3:    /* Minimum square */
			if(position++>minsqrwidth) {
				position = 0;
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
