/* $Id: gthJackAudio.cpp,v 1.12 2004/08/16 09:34:48 misha Exp $ */
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

void jack_shutdown (void *arg)
{
//	gthJackAudio *jout = (gthJackAudio *)arg;

	debug("shutting down");

	/* kill ourselves */
	kill (0, SIGTERM);
}

gthJackAudio::gthJackAudio (thSynth *argsynth)
	throw (thIOException)
{
	synth = argsynth;

	if ((jack_handle = jack_client_new("thinksynth")) == NULL)
	{
		throw errno;
	}

	chans = argsynth->GetChans();
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

//	jack_set_buffer_size(jack_handle, TH_WINDOW_LENGTH);

	debug("JACK sample rate is %d", jack_get_sample_rate(jack_handle));
	debug("JACK buffer size is %d", jack_get_buffer_size(jack_handle));

	debug("thinksynth sample rate is %li", argsynth->GetSamples());
	debug("thinksynth buffer size is %d", argsynth->GetWindowLen());

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

	chans = argsynth->GetChans();
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

//	jack_set_buffer_size(jack_handle, TH_WINDOW_LENGTH);
//	jack_set_buffer_size(jack_handle, argsynth->GetWindowLen());

	debug("JACK sample rate is %d", jack_get_sample_rate(jack_handle));
	debug("JACK buffer size is %d", jack_get_buffer_size(jack_handle));

	debug("thinksynth sample rate is %li", argsynth->GetSamples());
	debug("thinksynth buffer size is %d", argsynth->GetWindowLen());

	jack_on_shutdown (jack_handle, jack_shutdown, this);

	jack_set_process_callback(jack_handle, callback, this);
	jack_activate(jack_handle);
}

gthJackAudio::~gthJackAudio (void)
{
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
