/* $Id: wav.cpp,v 1.2 2003/09/14 20:51:24 misha Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

#include "thArg.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thMod.h"
#include "thSynth.h"
#include "thAudio.h"
#include "thException.h"
#include "thWav.h"

char		*desc = "Wav Input";
thPluginState	mystate = thActive;

extern "C" int	module_init (thPlugin *plugin);
extern "C" int	module_callback (thNode *node, thMod *mod, unsigned int windowlen);
extern "C" void module_cleanup (struct module *mod);

static thWav *thwav = NULL;

void module_cleanup (struct module *mod)
{
	printf("Wav Input plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	thwav = new_thWav("testwav.wav");

	printf("Wav Input plugin loading\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);
	
	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	thArg *out_arg = mod->GetArg(node, "out");
	signed short *buf = new signed short[windowlen];
	float *out = out_arg->allocate(windowlen);
	unsigned int i;

	thwav->Read(buf, windowlen);

	for(i = 0; i < windowlen; i++) {
		out[i] = (((float)buf[i])/32767)*TH_MAX;
		printf("writing %f\n", out[i]);
	}

	return 0;
}

