/* $Id: thALSAAudio.cpp,v 1.4 2004/04/08 00:34:56 misha Exp $ */

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include <alsa/asoundlib.h>

#include <gtkmm.h>

#include "think.h"

#include "thAudio.h"
#include "thALSAAudio.h"

thALSAAudio::thALSAAudio (thSynth *argsynth)
	throw (thIOException)
{
	synth = argsynth;

	if (snd_pcm_open (&play_handle, ALSA_DEFAULT_DEVICE,
					  SND_PCM_STREAM_PLAYBACK, 0) < 0)
	{
		fprintf(stderr, "thALSAAudio::thALSAAudio: %s\n", strerror(errno));
		throw errno;
	}

	open_seq();
	seq_nfds = snd_seq_poll_descriptors_count(seq_handle, POLLIN);
	nfds = snd_pcm_poll_descriptors_count (play_handle);
	pfds = (struct pollfd *)malloc(sizeof(struct pollfd) * 
								   (seq_nfds + nfds));
	snd_seq_poll_descriptors(seq_handle, pfds, seq_nfds, POLLIN);
	snd_pcm_poll_descriptors (play_handle, pfds+seq_nfds, nfds);

	SetFormat(argsynth);

	outbuf = NULL;
}


thALSAAudio::thALSAAudio (thSynth *argsynth, const char *device)
	throw (thIOException)
{
	if (snd_pcm_open (&play_handle, device, SND_PCM_STREAM_PLAYBACK, 0) < 0)
	{
		fprintf(stderr, "thALSAAudio::thALSAAudio: %s\n", strerror(errno));
		throw errno;
	}

	open_seq();
	seq_nfds = snd_seq_poll_descriptors_count(seq_handle, POLLIN);
	nfds = snd_pcm_poll_descriptors_count (play_handle);
	pfds = (struct pollfd *)alloca(sizeof(struct pollfd) * 
								   (seq_nfds + nfds));
	snd_seq_poll_descriptors(seq_handle, pfds, seq_nfds, POLLIN);
	snd_pcm_poll_descriptors (play_handle, pfds+seq_nfds, nfds);

	SetFormat(argsynth);

	outbuf = NULL;
}

thALSAAudio::~thALSAAudio ()
{
	snd_pcm_close(play_handle);

	if (outbuf)
	{
		free (outbuf);
	}
}

void thALSAAudio::SetFormat (thSynth *argsynth)
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

//	memcpy(&ifmt, afmt, sizeof(thAudioFmt));
//	memcpy(&ofmt, afmt, sizeof(thAudioFmt)); /* XXX */

	snd_pcm_hw_params_alloca(&hw_params);
	snd_pcm_hw_params_any(play_handle, hw_params);
	snd_pcm_hw_params_set_access(play_handle, hw_params,
								SND_PCM_ACCESS_RW_INTERLEAVED);

   snd_pcm_hw_params_set_format(play_handle, hw_params, SND_PCM_FORMAT_S16);
   
   snd_pcm_hw_params_set_rate_near(play_handle, hw_params, &samples, NULL);
   snd_pcm_hw_params_set_channels(play_handle, hw_params, chans);


   /* where the buffer is actually set */
//   snd_pcm_hw_params_set_periods(play_handle, hw_params, ifmt.channels, 0);
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

void thALSAAudio::SetFormat (const thAudioFmt *afmt)
{
	snd_pcm_hw_params_t *hw_params;
	snd_pcm_sw_params_t *sw_params;

	memcpy(&ifmt, afmt, sizeof(thAudioFmt));
	memcpy(&ofmt, afmt, sizeof(thAudioFmt)); /* XXX */

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
   
   snd_pcm_hw_params_set_rate_near(play_handle, hw_params, (unsigned int *)&ofmt.samples, NULL);
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


int thALSAAudio::Read (void *outbuf, int len)
{
	return -1;
}

int thALSAAudio::Write (float *inbuf, int len)
{
	int w = 0;
	int chans = ofmt.channels;

	/* malloc an appropriate buffer it would be *bad* if the length of the 
	   buffer passed in were to increase so don't do that (brandon) */
	if (!outbuf)
	{
		outbuf = malloc(len*(ofmt.bits/8)*chans);
		if (!outbuf)
		{
			fprintf(stderr,"thALSAAudio::Write: could not allocate buffer\n");
			exit(1);
		}
	}

	switch (ofmt.bits)
	{
		case 16:
		{
			signed short *buf = (signed short*)outbuf;
			/* convert to specified format */
			for (int i = 0; i < len * chans; i++)
			{ 
				le16(buf[i],(signed short)(((float)inbuf[i]/TH_MAX)*32767));
			}
			
			break;
		}
		default:
		{
			fprintf(stderr, "thALSAAudio::Write(): %d-bit audio unsupported!\n",
					ofmt.bits);
			exit(1);

			break;
		}
	}

	/* XXX: WTF? */
	unsigned char *buff = (unsigned char *)outbuf;

	w = snd_pcm_writei (play_handle, buff, len);

	if (w < 0)
	{
		/* under-run */
		if(w == -EPIPE)
		{
			debug("<< BUFFER UNDERRUN >> snd_pcm_prepare()");
			snd_pcm_prepare(play_handle);
		}
		else if (w == -ESTRPIPE)
		{
			debug("<< AUDIO SUSPENDED >>");

			while((w = snd_pcm_resume (play_handle)) == -EAGAIN)
			{
				sleep (1);
			}
			
			if (w < 0)
			{
				w = snd_pcm_prepare(play_handle);
			}

			debug("<< AUDIO UNSUSPENDED >>");
		}
		else
		{
			debug("<< UNKNOWN AUDIO WRITE ERROR >>\n");
		}
	}

	return w;
}

sigReadyWrite_t thALSAAudio::signal_ready_write (void)
{
	return m_sigReadyWrite;
}

sigMidiEvent_t thALSAAudio::signal_midi_event (void)
{
	return m_sigMidiEvent;
}

bool thALSAAudio::ProcessEvents (void)
{
	bool r = false;

	if (poll (pfds, seq_nfds + nfds, 1000) > 0)
	{
		int j;

		for (j = 0; j < seq_nfds; j++)
		{
			if(pfds[j].revents > 0)
			{
				m_sigMidiEvent.emit(seq_handle);
			}
		}
		for(j = seq_nfds; j < seq_nfds + nfds; j++)
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

bool thALSAAudio::open_seq (void)
{
    if (snd_seq_open(&seq_handle, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0)
	{
        fprintf(stderr, "Error opening ALSA sequencer.\n");
		return false;
    }

	snd_seq_set_client_name(seq_handle, "thinksynth");
	
    if (snd_seq_create_simple_port(seq_handle, "thinksynth",
        SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_APPLICATION) < 0)
	{
        fprintf(stderr, "Error creating sequencer port.\n");
		return false;
    }

	return true;
}