/* $Id: wav.cpp,v 1.10 2004/05/26 00:14:04 misha Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

//#include "thAudio.h"
//#include "thWav.h"

char		*desc = "Wav Input (BROKEN)";
thPluginState	mystate = thActive;
//static thWav *thwav = NULL;

void module_cleanup (struct module *mod)
{
	printf("Wav Input plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
//	thwav = new_thWav("testwav.wav");

	printf("Wav Input plugin loading\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);
	
	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
{
/*
	thArg *out_arg = mod->GetArg(node, "out");
	thArg *out_play = mod->GetArg(node, "play");
	signed short buf[windowlen * thwav->GetChannels()];
	float *out = out_arg->Allocate(windowlen);
	float *play = out_play->Allocate(windowlen);
	unsigned int i;
	int channels = thwav->GetChannels();

	thwav->Read(buf, windowlen * channels);

	for(i = 0; i < windowlen; i++) {
		out[i] = (((float)buf[i * channels]) / 32767) * TH_MAX;
		play[i] = !thwav->CheckEOF();
		} */

	return 0;
}

