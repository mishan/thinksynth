/* $Id$ */
/*
 * Copyright (C) 2004 Metaphonic Labs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <alsa/asoundlib.h>

#include "think.h"

char		*desc = "ALSA Input";
thPlugin::State	mystate = thPlugin::ACTIVE;

snd_pcm_t *cap_handle = NULL;

void module_cleanup (struct module *mod)
{
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

 	plugin->setDesc (desc);
	plugin->setState (mystate);
	
	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
{
	int channels = 2;
	thArg *out_arg = mod->getArg(node, "out");
	thArg *out_play = mod->getArg(node, "play");
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

