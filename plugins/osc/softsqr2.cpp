/* $Id: softsqr2.cpp,v 1.5 2003/05/11 08:06:24 aaronl Exp $ */

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

char		*desc = "Square wave with sine-like transitions, proportional to the frequency";
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
	float *out = new float[windowlen];
	float *last = new float[2];
	float wavelength, ratio;
	float sinewidth, minsqrwidth, maxsqrwidth;
	int position, phase;
	thArgValue *in_freq, *in_pw, *in_sw, *in_position;

	in_freq = (thArgValue *)mod->GetArg(node, "freq");
	in_pw = (thArgValue *)mod->GetArg(node, "pw"); // Pulse Width
	in_sw = (thArgValue *)mod->GetArg(node, "sw"); // Sine Width
	in_position = (thArgValue *)mod->GetArg(node, "position");

	position = (int)(*in_position)[0]; // Where in the phase we are
	phase = (int)(*in_position)[1]; // Which phase are we in
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
	
	node->SetArg("out", out, windowlen);
	last[0] = (float)position;
	last[1] = (float)phase;
	node->SetArg("position", (float*)last, 2);
	
	return 0;
}
