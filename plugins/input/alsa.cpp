/* $Id: alsa.cpp,v 1.7 2004/02/22 04:02:56 ink Exp $ */

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
	snd_pcm_hw_params_t *hw_params;
	snd_pcm_sw_params_t *sw_params;


	/* XXX: hardcoded device */
	if(snd_pcm_open (&cap_handle, "hw:0", SND_PCM_STREAM_CAPTURE, 0) < 0)
	{
		fprintf(stderr, "input::alsa::init: %s\n", strerror(errno));
		return 1;
	}

	snd_pcm_hw_params_alloca(&hw_params);
	snd_pcm_hw_params_any(cap_handle, hw_params);
	snd_pcm_hw_params_set_access(cap_handle, hw_params,
								SND_PCM_ACCESS_RW_INTERLEAVED);


	unsigned int rate = TH_SAMPLE;
	snd_pcm_hw_params_set_rate_near(cap_handle, hw_params, &rate, NULL);
	printf("input::alsa: set rate to %d\n", rate);
	snd_pcm_hw_params(cap_handle, hw_params);

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

	if(snd_pcm_readi(cap_handle, buf, windowlen) < 0)
	{
		fprintf(stderr, "input::alsa: buffer underrun\n");
		snd_pcm_prepare(cap_handle);
	}

	for(i = 0; i < windowlen; i++) {
		out[i] = (((float)buf[i * channels]) / 32767) * TH_MAX;
		play[i] = 1;  /* playback does not end until alsa closes, but we
						 don't handle that yet */
	}

	return 0;
}

