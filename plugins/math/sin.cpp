/* $Id: sin.cpp,v 1.2 2004/03/26 09:50:33 joshk Exp $ */

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

char		*desc = "Sine Calculation";
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
	thArg *in_index, *in_wavelength, *in_amp;
	thArg *out_arg;
	unsigned int i;
	float amp, wavelength;

	in_index = mod->GetArg(node, "index");
	in_wavelength = mod->GetArg(node, "wavelength");
	in_amp = mod->GetArg(node, "amp");

	out_arg = mod->GetArg(node, "out");
	out = out_arg->Allocate(windowlen);

	for(i=0;i<windowlen;i++)
	{
		amp = (*in_amp)[i];
		wavelength = (*in_wavelength)[i];

		if (amp == 0)
		{
			amp = 1;
		}
		if (wavelength == 0)
		{
			wavelength = 1;
		}

		out[i] = sin(((*in_index)[i] / wavelength) * M_PI * 2) * amp;
	}

	return 0;
}

