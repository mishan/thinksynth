/* $Id: blackman.cpp,v 1.4 2004/03/26 09:50:33 joshk Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

#include "thArg.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thMod.h"
#include "thSynth.h"


char		*desc = "Generates a sine impulse";
thPluginState	mystate = thPassive;





void module_cleanup (struct module *mod)
{
	printf("Sine plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("Sine plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	float *out;
	thArg *in_len, *in_cut;
	thArg *out_arg;
	float factor, len, k = 0;
	unsigned int i;

	in_len = mod->GetArg(node, "len"); /* Length of output */
	in_cut = mod->GetArg(node, "cutoff"); /* 0 - 1 */
	len = (int)(*in_len)[0];
	factor = (*in_cut)[0]/2;

	out_arg = mod->GetArg(node, "out");
	out = out_arg->Allocate((int)len+1);
	
	for(i=0;i<=(unsigned int)len;i++) {
		if(i == len/2) {
			out[i] = 2*M_PI*factor;
		} else {
			out[i] = (sin(2*M_PI*factor*(i-len/2))/(i-len/2))*(0.42-0.5*cos((2*M_PI*i)/len)+0.08*cos((4*M_PI*i)/len));
		}
		k += out[i];  /* We need to make all the samples add up to 1 */
	}

	k = 1/k;
	for(i=0;i<len;i++) {
		out[i] *= k;
	}

	return 0;
}

