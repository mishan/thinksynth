/* $Id: gthJackAudio.cpp,v 1.17 2004/09/16 07:59:06 misha Exp $ */
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
//	gthJackAudio *jout = (gthJackAudio *)arg;

	debug("shutting down");

	/* kill ourselves */
//	kill (0, SIGTERM);
}

gthJackAudio::gthJackAudio (thSynth *argsynth)
	throw (thIOException)
{
	synth = argsynth;

	if ((jack_handle = jack_client_new("thinksynth")) == NULL)
	{
		throw errno;
	}

	registerPorts();

//	jack_set_buffer_size(jack_handle, TH_WINDOW_LENGTH);

	getStats();

	jack_on_shutdown (jack_handle, jack_shutdown, this);
}

gthJackAudio::gthJackAudio (thSynth *argsynth, int (*callback)(jack_nframes_t,
															   void *))
	throw (thIOException)
{
  	synth = argsynth;

	if ((jack_handle = jack_client_new("thinksynth")) == NULL)
	{	
		throw errno;
	}

	registerPorts();
	
//	jack_set_buffer_size(jack_handle, TH_WINDOW_LENGTH);
//	jack_set_buffer_size(jack_handle, argsynth->GetWindowLen());

	getStats();

	jack_on_shutdown (jack_handle, jack_shutdown, this);

	jack_set_process_callback(jack_handle, callback, this);
	jack_activate(jack_handle);
}

bool gthJackAudio::tryConnect (bool connect)
{
	static int connected = 0;
	string output;

	if (!jack_handle)
	{
		debug("Called with a NULL jack_handle!");
		return false;
	}
	
	if (connected && connect)
	{
		debug("already connected...");
		return true;
	}

	/* Now try to connect */
	if (jack_port_by_name(jack_handle, "alsa_pcm:playback_1"))
		output = "alsa_pcm";
	else if (jack_port_by_name(jack_handle, "oss:playback_1") != NULL)
		output = "oss";

	if (output != "")
	{
		int error = 0;
		size_t jacklen = output.size() + 12; /* ???:playback_N + NUL */
#define thlen 17 /* thinksynth:out_N + NUL */
		char *out = (char*)malloc(jacklen), *ths = (char*)malloc(thlen);
		int i;

		for (i = 1; i <= 2; i++)
		{
			snprintf(out, jacklen, "%s:playback_%d", output.c_str(), i);
			snprintf(ths, thlen, "thinksynth:out_%d", i);
			if (connect)
			{
				if (jack_connect(jack_handle, ths, out) != 0)
				{
					fprintf(stderr, "warning: Could not connect JACK %s -> %s\n",
						ths, out);
					error = 1;
				}
			}
			else
			{
				if (jack_disconnect(jack_handle, ths, out) != 0)
				{
					fprintf(stderr, "warning: Could not disconnect from JACK %s -> %s\n",
						ths, out);
					error = 1;
				}
			}
		}
		free (out);
		free (ths);

		if (!error)
		{
			printf(connect ? "JACK connection made to '%s'\n" : "JACK disconnected from '%s'\n", output.c_str());
			connected = connect;
		}
	}
	else
	{
		fprintf(stderr, "warning: no suitable JACK playback targets found\n");
	}
	return connected;
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
