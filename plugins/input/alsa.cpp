/* $Id: alsa.cpp,v 1.1 2004/01/31 12:25:13 misha Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <alsa/asoundlib.h>

#include "think.h"

#include "thArg.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thMod.h"
#include "thSynth.h"
#include "thAudio.h"
#include "thException.h"
#include "thALSAAudio.h"

char		*desc = "ALSA Input";
thPluginState	mystate = thActive;

extern "C" int	module_init (thPlugin *plugin);
extern "C" int	module_callback (thNode *node, thMod *mod, unsigned int windowlen);
extern "C" void module_cleanup (struct module *mod);

snd_pcm_t *cap_handle = NULL;

void module_cleanup (struct module *mod)
{
	printf("ALSA Input plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	/* XXX: hardcoded device */
	if(snd_pcm_open (&cap_handle, "hw:0", SND_PCM_STREAM_CAPTURE, 0) < 0)
	{
		fprintf(stderr, "input::alsa::init: %s\n", strerror(errno));
		return 1;
	}

	printf("ALSA Input plugin loading\n");

 	plugin->SetDesc (desc);
	plugin->SetState (mystate);
	
	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	int channels = 2;
	thArg *out_arg = mod->GetArg(node, "out");
	thArg *out_play = mod->GetArg(node, "play");
	signed short buf[windowlen * channels];
	float *out = out_arg->Allocate(windowlen);
	float *play = out_play->Allocate(windowlen);
	unsigned int i;

	snd_pcm_readi(cap_handle, buf, windowlen);

	for(i = 0; i < windowlen; i++) {
		out[i] = (((float)buf[i * channels]) / 32767) * TH_MAX;
//		play[i] = !thwav->CheckEOF();
	}

	return 0;
}

