/* $Id: gthJackAudio.cpp,v 1.6 2004/05/09 01:04:38 misha Exp $ */

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

	out_1 = jack_port_register(jack_handle, "out_1",
							   JACK_DEFAULT_AUDIO_TYPE, 
							   JackPortIsOutput|JackPortIsTerminal, 0);
	
	out_2 = jack_port_register(jack_handle, "out_2",
							   JACK_DEFAULT_AUDIO_TYPE, 
							   JackPortIsOutput|JackPortIsTerminal, 0);

//	jack_set_buffer_size(jack_handle, TH_WINDOW_LENGTH);

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
