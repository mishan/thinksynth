/* $Id: thALSAAudio.cpp,v 1.16 2004/02/13 08:27:58 misha Exp $ */

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "thAudio.h"
#include "thALSAAudio.h"
#include "thEndian.h"

#include "think.h"

thALSAAudio::thALSAAudio (const thAudioFmt *afmt)
	throw (thIOException)
{
	if (snd_pcm_open (&play_handle, ALSA_DEFAULT_DEVICE,
					  SND_PCM_STREAM_PLAYBACK, 0) < 0) {
		fprintf(stderr, "thALSAAUdio::SetFormat: %s\n", strerror(errno));
		return;
	}

	SetFormat(afmt);

	outbuf = NULL;
}


thALSAAudio::thALSAAudio (const char *device, const thAudioFmt *afmt)
	throw (thIOException)
{
	if (snd_pcm_open (&play_handle, device, SND_PCM_STREAM_PLAYBACK, 0) < 0) {
		fprintf(stderr, "thALSAAUdio::SetFormat: %s\n", strerror(errno));
		return;
	}

	SetFormat(afmt);

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

   switch(ifmt.bits) {
	   case 8:
		   snd_pcm_hw_params_set_format(play_handle, hw_params,
										SND_PCM_FORMAT_U8);
		   break;
	   case 16:
		   snd_pcm_hw_params_set_format(play_handle, hw_params,
										SND_PCM_FORMAT_S16);
		   break;
   }
   
   snd_pcm_hw_params_set_rate_near(play_handle, hw_params, (unsigned int *)&ifmt.samples, NULL);
   snd_pcm_hw_params_set_channels(play_handle, hw_params, ifmt.channels);


   /* where the buffer is actually set */
//   snd_pcm_hw_params_set_periods(play_handle, hw_params, ifmt.channels, 0);
   snd_pcm_hw_params_set_periods(play_handle, hw_params, ifmt.channels, 0);
   snd_pcm_hw_params_set_period_size(play_handle, hw_params,
									 TH_BUFFER_PERIOD, 0);
   
//	snd_pcm_hw_params_set_buffer_time(play_handle, hw_params, 1000, 0);
   snd_pcm_hw_params(play_handle, hw_params);
   snd_pcm_sw_params_alloca(&sw_params);
   snd_pcm_sw_params_current(play_handle, sw_params);
   snd_pcm_sw_params_set_avail_min(play_handle, sw_params, ifmt.period);
   snd_pcm_sw_params(play_handle, sw_params);
}

int thALSAAudio::Read (void *outbuf, int len)
{
	return -1;
}

int thALSAAudio::Write (float *inbuf, int len)
{
	int i, w = 0;
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
			for (i = 0; i < len * chans; i++){ 
				le16(buf[i],(signed short)(((float)inbuf[i]/TH_MAX)*32767));
			}
		}
		default:
		{
			fprintf(stderr, "thOSSAudio::Write(): %d-bit audio unsupported!\n",
					ofmt.bits);
			exit(1);
		}
	}

	/* XXX: WTF? */
	unsigned char *buff = (unsigned char *)outbuf;

//	w = write(fd, buff, len*bytes);
	w = snd_pcm_writei (play_handle, buff, len);

	return w;
}
