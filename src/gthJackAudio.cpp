/* $Id: gthJackAudio.cpp,v 1.2 2004/04/15 09:38:42 misha Exp $ */

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

	if((output_port = jack_port_register(jack_handle, "Out",
									JACK_DEFAULT_AUDIO_TYPE, 
									JackPortIsOutput|JackPortIsTerminal, 0))
	   == NULL)
	{
		throw errno;
	}

	jack_set_buffer_size(jack_handle, TH_WINDOW_LENGTH);

	debug("sample rate is %d\n", jack_get_sample_rate(jack_handle));
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
