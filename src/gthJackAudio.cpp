/* $Id: gthJackAudio.cpp,v 1.7 2004/05/11 02:16:31 misha Exp $ */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <jack/jack.h>

#include "think.h"

#include "thAudio.h"
#include "thfJackAudio.h"

thfJackAudio::thfJackAudio (thSynth *argsynth)
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

	debug("sample rate is %d\n", jack_get_sample_rate(jack_handle));
}

thfJackAudio::thfJackAudio (thSynth *argsynth, int (*callback)(jack_nframes_t,
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

	debug("sample rate is %d\n", jack_get_sample_rate(jack_handle));

	jack_set_process_callback(jack_handle, callback, this);
	jack_activate(jack_handle);
}

thfJackAudio::~thfJackAudio (void)
{
}

void thfJackAudio::Play (thAudio *audioPtr)
{
	return;
}

int thfJackAudio::Write (float *buf, int len)
{
	return 0;
}

int thfJackAudio::Read (void *buf, int len)
{
	return 0;
}

void thfJackAudio::SetFormat (thSynth *argsynth)
{
}

void thfJackAudio::SetFormat (const thAudioFmt *afmt)
{
}

bool thfJackAudio::ProcessEvents (void)
{
	return false;
}

void *thfJackAudio::GetOutBuf (int argchan, jack_nframes_t nframes)
{
	if ((argchan < 0) || (argchan >= chans))
		return NULL;

	return jack_port_get_buffer(out_ports[argchan], nframes);
}
