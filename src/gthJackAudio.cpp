/* $Id$ */
/*
 * Copyright (C) 2004-2005 Metaphonic Labs
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
	jout->invalidate();
}

gthJackAudio::gthJackAudio (thSynth *synth)
	throw (thIOException)
{
	synth_ = synth;
	jcallback_ = NULL;

	if ((jack_handle_ = jack_client_new("thinksynth")) == NULL)
	{
		throw errno;
	}

	registerPorts();

	jack_on_shutdown (jack_handle_, jack_shutdown, this);

	printf("attempting to set synth parameters to match JACK\n");
	synth_->setSampleRate(jack_get_sample_rate(jack_handle_));
//	synth_->setWindowLen(jack_get_buffer_size(jack_handle_));

	getStats();
}

gthJackAudio::gthJackAudio (thSynth *synth,
							int (*callback)(jack_nframes_t, void *))
	throw (thIOException)
{
  	synth_ = synth;
	jcallback_ = callback;

	if ((jack_handle_ = jack_client_new("thinksynth")) == NULL)
	{	
		throw errno;
	}

	registerPorts();

	jack_on_shutdown (jack_handle_, jack_shutdown, this);

	printf("attempting to set synth parameters to match JACK\n");
	synth->setSampleRate(jack_get_sample_rate(jack_handle_));

	if ((unsigned int)synth_->getWindowlen() !=
		jack_get_buffer_size(jack_handle_))
	{
		fprintf(stderr, "WARNING! thinksynth's buffer size is different from that of JACK's. Currently this makes everything sound bad.. Try re-running thinksynth with the -l [windowlen] argument.\n");
	}
//	synth->setWindowLen(jack_get_buffer_size(jack_handle));
	synth_->process();

	getStats();

	jack_set_process_callback(jack_handle_, callback, this);
	jack_activate(jack_handle_);
}

int gthJackAudio::tryConnect (bool connect)
{
	static bool connected = false;
	int error = 0;
	string output;

	if (jack_handle_ == NULL)
	{
		/* JACK died while we were connected; this is OK. Pretend
		 * we actually tried to disconnect */
		if (!connect)
		{
			connected = false;
			return 0;
		}
	
		/* try to allocate a new one */
		if ((jack_handle_ = jack_client_new("thinksynth")) == NULL)
			return ERR_HANDLE_NULL;

		registerPorts();
		jack_on_shutdown (jack_handle_, jack_shutdown, this);
		puts("JACK revived; setting synth params");
		synth_->setSampleRate(jack_get_sample_rate(jack_handle_));
		synth_->process();
		
		if (jcallback_ != NULL)
			jack_set_process_callback(jack_handle_, jcallback_, this);
		jack_activate(jack_handle_);
	}
	
	if (connected && connect)
	{
		debug("already connected...");
		return 0;
	}

	const char** ports = jack_get_ports(jack_handle_, NULL, NULL, 0);
	const char** ptr;
	bool portaudio = false;

	for (ptr = ports; *ptr != NULL; ptr++)
	{
		if (!strncmp(*ptr, "alsa_pcm", 8))
		{
			output = "alsa_pcm";
			break;
		}
		else if (!strncmp(*ptr, "oss", 3))
		{
			output = "oss";
			break;
		}
		else if (!strncmp(*ptr, "portaudio", 9))
		{
			/* Look for the : */
			char* ptr2 = strdup(*ptr);
			char* tmp = ptr2;
			
			while(*tmp++ && *tmp != ':');
			while(*tmp++ && *tmp != ':');

			*tmp = '\0';

			output = ptr2;
			portaudio = true;

			break;
		}
	}

	if (output != "")
	{
		size_t jacklen;

		if (portaudio)
			jacklen = output.size() + 11; /* ???:playbackN + NUL */
		else
			jacklen = output.size() + 12; /* ???:playback_N + NUL */
			
#define thlen 17 /* thinksynth:out_N + NUL */
		char *out = new char[jacklen], *ths = new char[thlen];
		int i;

		for (i = 1; i <= synth_->audioChannelCount(); i++)
		{
			snprintf(out, jacklen, "%s:playback%s%d", output.c_str(),
				portaudio ? "" : "_", i);
			snprintf(ths, thlen, "thinksynth:out_%d", i);
			if (connect)
			{
				if ((error = jack_connect(jack_handle_, ths, out)) != 0)
				{
					fprintf(stderr, "warning: Could not connect JACK %s -> %s\n",
						ths, out);
				}
			}
			else
			{
				if ((error = jack_disconnect(jack_handle_, ths, out)) != 0)
				{
					fprintf(stderr, "warning: Could not disconnect from JACK %s -> %s\n",
						ths, out);
				}
			}
		}

		delete[] out;
		delete[] ths;

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
	if (jack_handle_ == NULL)
	{
		debug("Called with a NULL jack_handle!");
		return;
	}
	
	chans_ = synth_->audioChannelCount();
	out_ports_ = new jack_port_t *[chans_];

	for (int i = 0; i < chans_; i++)
	{
		char pstr[8];
		sprintf(pstr, "out_%d", i+1);

		out_ports_[i] = jack_port_register(jack_handle_, pstr,
										  JACK_DEFAULT_AUDIO_TYPE, 
										  JackPortIsOutput|JackPortIsTerminal,
										  0);
	}
}

void gthJackAudio::getStats (void)
{
	if (jack_handle_ == NULL)
	{
		debug("Called with a NULL jack_handle!");
		return;
	}

  	printf("JACK sample rate is %d\n", jack_get_sample_rate(jack_handle_));
	printf("JACK buffer size is %d\n", jack_get_buffer_size(jack_handle_));
	printf("JACK is %srealtime\n", jack_is_realtime(jack_handle_) ? "" : "not ");

	printf("thinksynth sample rate is %li\n", synth_->getSampleRate());
	printf("thinksynth buffer size is %d\n", synth_->getWindowlen());
}

gthJackAudio::~gthJackAudio (void)
{
	if (jack_handle_)
		jack_deactivate(jack_handle_);
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
	if ((argchan < 0) || (argchan >= chans_))
		return NULL;

	return jack_port_get_buffer(out_ports_[argchan], nframes);
}


int gthJackAudio::getSampleRate (void)
{
	return jack_get_sample_rate(jack_handle_);
}

int gthJackAudio::getBufferSize (void)
{
	return jack_get_buffer_size(jack_handle_);
}

void gthJackAudio::invalidate (void)
{	
	jack_handle_ = NULL;
	tryConnect(false);
}
