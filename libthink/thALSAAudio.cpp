/* $Id: thALSAAudio.cpp,v 1.2 2004/01/25 11:51:51 misha Exp $ */

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "thAudio.h"
#include "thALSAAudio.h"
#include "thEndian.h"

#include "think.h"

thALSAAudio::thALSAAudio (char *null, const thAudioFmt *afmt)
	throw (thIOException)
{
	SetFormat(afmt);

	outbuf = NULL;
}

thALSAAudio::~thALSAAudio ()
{
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

	if (snd_pcm_open (&play_handle, "hw:0", SND_PCM_STREAM_PLAYBACK, 0) < 0) {
		fprintf(stderr, "thALSAAUdio::SetFormat: %s\n", strerror(errno));
	}

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
    snd_pcm_hw_params_set_rate_near(play_handle, hw_params, ifmt.samples, 0);
    snd_pcm_hw_params_set_channels(play_handle, hw_params, ifmt.channels);
    snd_pcm_hw_params_set_periods(play_handle, hw_params, 2, 0);
    snd_pcm_hw_params_set_period_size(play_handle, hw_params, ifmt.period, 0);
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
	int i ,w = 0;
	int chans = ofmt.channels;
	int bytes = ofmt.bits / 8;
	int samplelen = bytes*chans;

		/* malloc an appropriate buffer it would be *bad* if the length of the 
	   buffer passed in were to increase so don't do that (brandon) */
	if (!outbuf){
		outbuf = malloc(len*bytes*chans);
		if (!outbuf){
			fprintf(stderr,"thALSAAudio::Write -- could not allocate buffer\n");
			exit(1);
		}
	}
	if (bytes == 2)
	{
		signed short *buf = (signed short*)outbuf;
		/* convert to specified format */
		for (i = 0; i < len * chans; i++){ 
			le16(buf[i],(signed short)(((float)inbuf[i]/TH_MAX)*32767));
		}
	}
	else
	{
		fprintf(stderr,"\tthOSSAudio::Write() error: %d-bit audio unsupported!\n",ofmt.bits);
		exit(1);
	}

	/* XXX: WTF? */
	unsigned char *buff = (unsigned char *)outbuf;

//	w = write(fd, buff, len*bytes);
	snd_pcm_writei (play_handle, buff, len);

	return w;
}
