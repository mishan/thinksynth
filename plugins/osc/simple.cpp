#define USE_PLUGIN

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

#define DEGRAD 0.017453

char		*desc = "Basic Oscillator";
thPluginState	mystate = thActive;

extern "C" int	module_init (int version, thPlugin *plugin);
extern "C" int	module_callback (void *node, void *mod, unsigned int windowlen);
extern "C" void module_cleanup (struct module *mod);

void module_cleanup (struct module *mod)
{
	printf("Oscillator module unloading\n");
}

/* ModuleLoad() invokes this function with a pointer to the plugin
 * instance. */
int module_init (int version, thPlugin *plugin)
{
	printf("Simple Oscillator plugin loaded\n");
	plugin->SetDesc (desc);
	plugin->SetState (mystate);
	
	return 0;
}

int module_callback (void *node, void *mod, unsigned int windowlen)
{
	int i;
	float *out = new float[windowlen];
	float *last[1];
	float wavelength, position;
	thArgValue *in_freq, *in_pw, *in_waveform, *in_last;

	in_freq = ((thArgValue*)((thMod*)mod)->GetArg(((thNode*)node)->GetName(), "freq"));
	in_pw = ((thArgValue*)((thMod*)mod)->GetArg(((thNode*)node)->GetName(), "pw"));
	in_waveform = ((thArgValue*)((thMod*)mod)->GetArg(((thNode*)node)->GetName(), "waveform"));
	in_last = ((thArgValue*)((thMod*)mod)->GetArg(((thNode*)node)->GetName(), "last"));

	for(i=0; i < (int)windowlen; i++) {
	  wavelength = TH_SAMPLE * (1.0/in_freq->argValues[i]);
	  if(++position > wavelength) {
	    position = 0;
	  }

	  switch((int)in_waveform->argValues[i]) {
	    /* 0 = sine, 1 = sawtooth, 2 = square, 3 = tri */
	  case 0:
	    out[i] = TH_RANGE*sin((position/wavelength)*360*DEGRAD)+TH_MIN;
	    /* 360*DEGRAD = whack, make this better */
	    break;
	  case 1:
	    out[i] = TH_RANGE*(position/wavelength)+TH_MIN;
	    break;
	    /* XXX  Add the rest of these  =] */
	  }
	  out[i] = TH_RANGE*(rand()/(RAND_MAX+1.0))+TH_MIN;
	}

	((thNode*)node)->SetArg("out", out, windowlen);
	*last[0] = position;
	((thNode*)node)->SetArg("last", (float*)last, 1);

	return 0;
}
