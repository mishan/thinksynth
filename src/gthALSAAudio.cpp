/* $Id: gthALSAAudio.cpp,v 1.8 2004/09/16 09:14:15 misha Exp $ */
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

#include <alsa/asoundlib.h>

#include <gtkmm.h>

#include "think.h"

#include "gthAudio.h"
#include "gthALSAAudio.h"

gthALSAAudio::gthALSAAudio (thSynth *argsynth)
	throw (thIOException)
{
	synth = argsynth;

	if (snd_pcm_open (&play_handle, ALSA_DEFAULT_AUDIO_DEVICE,
					  SND_PCM_STREAM_PLAYBACK, 0) < 0)
	{
		fprintf(stderr, "gthALSAAudio::gthALSAAudio: %s\n", strerror(errno));
		throw errno;
	}

	nfds = snd_pcm_poll_descriptors_count (play_handle);
	pfds = (struct pollfd *)malloc(sizeof(struct pollfd) * nfds);
	snd_pcm_poll_descriptors (play_handle, pfds, nfds);

	SetFormat(argsynth);

	outbuf = NULL;

	thread = Glib::Thread::create(SigC::slot(*this, &gthALSAAudio::main), false);
//	thread->set_priority(Glib::THREAD_PRIORITY_URGENT);
}

gthALSAAudio::gthALSAAudio (thSynth *argsynth, const char *device)
	throw (thIOException)
{
	if (snd_pcm_open (&play_handle, device, SND_PCM_STREAM_PLAYBACK, 0) < 0)
	{
		fprintf(stderr, "gthALSAAudio::gthALSAAudio: %s\n", strerror(errno));
		throw errno;
	}

	nfds = snd_pcm_poll_descriptors_count (play_handle);
	pfds = (struct pollfd *)malloc(sizeof(struct pollfd) * nfds);
	snd_pcm_poll_descriptors (play_handle, pfds, nfds);

	SetFormat(argsynth);

	outbuf = NULL;

	thread = Glib::Thread::create(SigC::slot(*this, &gthALSAAudio::main), false);
//	thread->set_priority(Glib::THREAD_PRIORITY_URGENT);
}

gthALSAAudio::~gthALSAAudio ()
{
	snd_pcm_close(play_handle);

	if (outbuf)
	{
		free (outbuf);
	}

	free (pfds);
}

void gthALSAAudio::SetFormat (thSynth *argsynth)
{
	snd_pcm_hw_params_t *hw_params;
	snd_pcm_sw_params_t *sw_params;
	unsigned int samples = TH_SAMPLE;
	unsigned int period = argsynth->GetWindowLen();
	unsigned int chans = argsynth->GetChans();

	ofmt.bits = 16;
	ofmt.samples = samples;
	ofmt.period = period;
	ofmt.channels = chans;

	snd_pcm_hw_params_alloca(&hw_params);
	snd_pcm_hw_params_any(play_handle, hw_params);
	snd_pcm_hw_params_set_access(play_handle, hw_params,
								SND_PCM_ACCESS_RW_INTERLEAVED);

	snd_pcm_hw_params_set_format(play_handle, hw_params, SND_PCM_FORMAT_S16);
   
	snd_pcm_hw_params_set_rate_near(play_handle, hw_params, &samples, NULL);
	snd_pcm_hw_params_set_channels(play_handle, hw_params, chans);


   /* where the buffer is actually set */
	snd_pcm_hw_params_set_periods(play_handle, hw_params, chans, 0);
	snd_pcm_hw_params_set_period_size(play_handle, hw_params,
									 TH_BUFFER_PERIOD, 0);
   
//	snd_pcm_hw_params_set_buffer_time(play_handle, hw_params, 1000, 0);

	snd_pcm_hw_params(play_handle, hw_params);

	snd_pcm_sw_params_alloca(&sw_params);
	snd_pcm_sw_params_current(play_handle, sw_params);
	snd_pcm_sw_params_set_avail_min(play_handle, sw_params, period);
	snd_pcm_sw_params(play_handle, sw_params);
}

void gthALSAAudio::SetFormat (const gthAudioFmt *afmt)
{
	snd_pcm_hw_params_t *hw_params;
	snd_pcm_sw_params_t *sw_params;

	memcpy(&ifmt, afmt, sizeof(gthAudioFmt));
	memcpy(&ofmt, afmt, sizeof(gthAudioFmt)); /* XXX */

	snd_pcm_hw_params_alloca(&hw_params);
	snd_pcm_hw_params_any(play_handle, hw_params);
	snd_pcm_hw_params_set_access(play_handle, hw_params,
								SND_PCM_ACCESS_RW_INTERLEAVED);
	switch(ifmt.bits)
	{
		case 8:
		{
			snd_pcm_hw_params_set_format(play_handle, hw_params,
										SND_PCM_FORMAT_U8);
			break;
		}
		case 16:
		{
			snd_pcm_hw_params_set_format(play_handle, hw_params,
										 SND_PCM_FORMAT_S16);
			break;
		}
	}
   
	snd_pcm_hw_params_set_rate_near(play_handle, hw_params, 
									(unsigned int *)&ofmt.samples, NULL);
	snd_pcm_hw_params_set_channels(play_handle, hw_params, ofmt.channels);


   /* where the buffer is actually set */
//   snd_pcm_hw_params_set_periods(play_handle, hw_params, ifmt.channels, 0);
	snd_pcm_hw_params_set_periods(play_handle, hw_params, ofmt.channels, 0);
	snd_pcm_hw_params_set_period_size(play_handle, hw_params,
									 TH_BUFFER_PERIOD, 0);
   
//	snd_pcm_hw_params_set_buffer_time(play_handle, hw_params, 1000, 0);
	snd_pcm_hw_params(play_handle, hw_params);
	snd_pcm_sw_params_alloca(&sw_params);
	snd_pcm_sw_params_current(play_handle, sw_params);
	snd_pcm_sw_params_set_avail_min(play_handle, sw_params, ofmt.period);
	snd_pcm_sw_params(play_handle, sw_params);
}


int gthALSAAudio::Read (void *outbuf, int len)
{
	return -1;
}

int gthALSAAudio::Write (float *inbuf, int len)
{
	int w = 0;
	int chans = ofmt.channels;
	int bufferoffset = 0;

	/* malloc an appropriate buffer it would be *bad* if the length of the 
	   buffer passed in were to increase so don't do that (brandon) */
	if (!outbuf)
	{
		outbuf = malloc(len*(ofmt.bits/8)*chans);
		if (!outbuf)
		{
			fprintf(stderr,"gthALSAAudio::Write: could not allocate buffer\n");
			exit(1);
		}
	}

	switch (ofmt.bits)
	{
		case 16:
		{
			/* XXX: WTF?!  LEARN TO CAST! someone fix this */
			signed short *buf = (signed short*)outbuf;
			/* convert to specified format */
			for (int i = 0; i < chans; i++)
			{
				for(int j = 0; j < len; j++)
				{
					le16(buf[j * chans + i], (signed short)(((float)inbuf[bufferoffset + j]/TH_MAX)*32767));
				}
				bufferoffset += len;
			}
			
			break;
		}
		default:
		{
			fprintf(stderr, "gthALSAAudio::Write(): %d-bit audio unsupported!\n",
					ofmt.bits);
			exit(1);

			break;
		}
	}

	/* XXX: WTF? */
	// unsigned char *buff = (unsigned char *)outbuf; STOP SMOKING CRACK

	w = snd_pcm_writei (play_handle, (unsigned char*)outbuf, len);

	if (w < 0)
	{
		/* under-run */
		if(w == -EPIPE)
		{
			fprintf(stderr, "<< BUFFER UNDERRUN >> snd_pcm_prepare()\n");
			snd_pcm_prepare(play_handle);
		}
		else if (w == -ESTRPIPE)
		{
			fprintf(stderr, "<< AUDIO SUSPENDED >>");

			while((w = snd_pcm_resume (play_handle)) == -EAGAIN)
			{
				sleep (1);
			}
			
			if (w < 0)
			{
				w = snd_pcm_prepare(play_handle);
			}

			fprintf(stderr, "<< AUDIO UNSUSPENDED >>");
		}
		/* play handle got removed from under us, 99% of the time
		 * this means that a different thread nuked the handle
		 * after a USR1 */
		else if (w == -EBADFD)
		{
			debug("lost ALSA playback handle, exiting");
			exit(0);
		}
		else
		{
			fprintf(stderr, "<< UNKNOWN AUDIO WRITE ERROR: %s >>\n", snd_strerror(w));
		}
	}

	return w;
}

sigReadyWrite_t gthALSAAudio::signal_ready_write (void)
{
	return m_sigReadyWrite;
}

bool gthALSAAudio::ProcessEvents (void)
{
	bool r = false;

	return r;

	/* XXX */

	if (poll (pfds, nfds, 50) > 0)
	{
		int j;

		for(j = 0; j < nfds; j++)
		{
			if (pfds[j].revents > 0)
			{
				m_sigReadyWrite.emit();
				r = true;
			}
		}
	}

	return r;
}

bool gthALSAAudio::pollAudioEvent (Glib::IOCondition)
{
	m_sigReadyWrite();

	return true;
}

void gthALSAAudio::main (void)
{
	Glib::RefPtr<Glib::MainContext> loMain = Glib::MainContext::create();
	
	loMain->signal_io().connect(SigC::slot(*this,
										   &gthALSAAudio::pollAudioEvent),
									   pfds[0].fd, Glib::IO_OUT,
								Glib::PRIORITY_HIGH);

	while (1)
	{
		loMain->iteration (true);
	}
}
