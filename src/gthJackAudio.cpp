/* $Id: gthJackAudio.cpp,v 1.20 2004/09/18 02:16:47 joshk Exp $ */
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

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include <jack/jack.h>

#include "think.h"

#include "gthAudio.h"
#include "gthJackAudio.h"

/* XXX */
#include "gthPrefs.h"
extern gthPrefs *prefs;

void jack_shutdown (void *arg)
{
	gthJackAudio *jout = (gthJackAudio *)arg;
	debug("JACK server went away, thread shutting down");
	/* Invalidate */
	jout->jack_handle = NULL;
	jout->tryConnect(false);
}

gthJackAudio::gthJackAudio (thSynth *argsynth)
	throw (thIOException)
{
	synth = argsynth;
	jcallback = NULL;

	if ((jack_handle = jack_client_new("thinksynth")) == NULL)
	{
		throw errno;
	}

	registerPorts();

	jack_on_shutdown (jack_handle, jack_shutdown, this);

	printf("attempting to set synth parameters to match JACK\n");
	synth->setSamples(jack_get_sample_rate(jack_handle));
//	synth->setWindowLen(jack_get_buffer_size(jack_handle));

	getStats();
}

gthJackAudio::gthJackAudio (thSynth *argsynth, int (*callback)(jack_nframes_t,
															   void *))
	throw (thIOException)
{
  	synth = argsynth;
	jcallback = callback;

	if ((jack_handle = jack_client_new("thinksynth")) == NULL)
	{	
		throw errno;
	}

	registerPorts();

	jack_on_shutdown (jack_handle, jack_shutdown, this);

	printf("attempting to set synth parameters to match JACK\n");
	synth->setSamples(jack_get_sample_rate(jack_handle));
//	synth->setWindowLen(jack_get_buffer_size(jack_handle));
	synth->Process();

	getStats();

	jack_set_process_callback(jack_handle, callback, this);
	jack_activate(jack_handle);
}

int gthJackAudio::tryConnect (bool connect)
{
	static bool connected = false;
	int error = 0;
	string output;

	if (!jack_handle)
	{
		/* JACK died while we were connected; this is OK. Pretend
		 * we actually tried to disconnect */
		if (!connect)
		{
			connected = false;
			return 0;
		}
	
		/* try to allocate a new one */
		if ((jack_handle = jack_client_new("thinksynth")) == NULL)
			return ERR_HANDLE_NULL;

		registerPorts();
		jack_on_shutdown (jack_handle, jack_shutdown, this);
		puts("JACK revived; setting synth params");
		synth->setSamples(jack_get_sample_rate(jack_handle));
		synth->Process();
		
		if (jcallback != NULL)
			jack_set_process_callback(jack_handle, jcallback, this);
		jack_activate(jack_handle);
	}
	
	if (connected && connect)
	{
		debug("already connected...");
		return 0;
	}

	/* Now try to connect */
	if (jack_port_by_name(jack_handle, "alsa_pcm:playback_1") != NULL)
		output = "alsa_pcm";
	else if (jack_port_by_name(jack_handle, "oss:playback_1") != NULL)
		output = "oss";

	if (output != "")
	{
#define MAX_CHANS 2
		size_t jacklen = output.size() + 12; /* ???:playback_N + NUL */
#define thlen 17 /* thinksynth:out_N + NUL */
		char *out = new char[jacklen], *ths = new char[thlen];
		int i;

		for (i = 1; i <= MAX_CHANS; i++)
		{
			snprintf(out, jacklen, "%s:playback_%d", output.c_str(), i);
			snprintf(ths, thlen, "thinksynth:out_%d", i);
			if (connect)
			{
				if ((error = jack_connect(jack_handle, ths, out)) != 0)
				{
					fprintf(stderr, "warning: Could not connect JACK %s -> %s\n",
						ths, out);
				}
			}
			else
			{
				if ((error = jack_disconnect(jack_handle, ths, out)) != 0)
				{
					fprintf(stderr, "warning: Could not disconnect from JACK %s -> %s\n",
						ths, out);
				}
			}
		}

		delete out;
		delete ths;

		if (error == 0)
		{
			printf(connect ? "JACK connection made to '%s'\n" : "JACK disconnected from '%s'\n", output.c_str());
			connected = connect;
		}
	}
	else
	{
		error = ERR_NO_PLAYBACK;
		fprintf(stderr, "warning: no suitable JACK playback targets found\n");
	}

	return error;
}

void gthJackAudio::registerPorts (void)
{
	if (!jack_handle)
	{
		debug("Called with a NULL jack_handle!");
		return;
	}
	
	chans = synth->GetChans();
	out_ports = new jack_port_t *[chans];

	for (int i = 0; i < chans; i++)
	{
		char pstr[8];
		sprintf(pstr, "out_%d", i+1);

		out_ports[i] = jack_port_register(jack_handle, pstr,
										  JACK_DEFAULT_AUDIO_TYPE, 
										  JackPortIsOutput|JackPortIsTerminal,
										  0);
	}
}

void gthJackAudio::getStats (void)
{
	if (!jack_handle)
	{
		debug("Called with a NULL jack_handle!");
		return;
	}

  	printf("JACK sample rate is %d\n", jack_get_sample_rate(jack_handle));
	printf("JACK buffer size is %d\n", jack_get_buffer_size(jack_handle));
	printf("JACK is %srealtime\n", jack_is_realtime(jack_handle) ? "" : "not ");

	printf("thinksynth sample rate is %li\n", synth->GetSamples());
	printf("thinksynth buffer size is %d\n", synth->GetWindowLen());
}

gthJackAudio::~gthJackAudio (void)
{
	if (jack_handle)
		jack_deactivate(jack_handle);
}

int gthJackAudio::Write (float *buf, int len)
{
	return 0;
}

int gthJackAudio::Read (void *buf, int len)
{
	return 0;
}

void gthJackAudio::SetFormat (thSynth *argsynth)
{
}

void gthJackAudio::SetFormat (const gthAudioFmt *afmt)
{
}

bool gthJackAudio::ProcessEvents (void)
{
	return false;
}

void *gthJackAudio::GetOutBuf (int argchan, jack_nframes_t nframes)
{
	if ((argchan < 0) || (argchan >= chans))
		return NULL;

	return jack_port_get_buffer(out_ports[argchan], nframes);
}


int gthJackAudio::getSampleRate (void)
{
	return jack_get_sample_rate(jack_handle);
}

int gthJackAudio::getBufferSize (void)
{
	return jack_get_buffer_size(jack_handle);
}
