/* $Id: gthJackAudio.cpp,v 1.5 2004/05/08 22:49:50 misha Exp $ */

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

	if((out_1 = jack_port_register(jack_handle, "out_1",
								   JACK_DEFAULT_AUDIO_TYPE, 
								   JackPortIsOutput, 0)) == NULL)
	{
		throw errno;
	}

	if((out_2 = jack_port_register(jack_handle, "out_2",
								   JACK_DEFAULT_AUDIO_TYPE, 
								   JackPortIsOutput, 0)) == NULL)
	{
		throw errno;
	}

//	jack_set_buffer_size(jack_handle, 2048);

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
