/* $Id: thOSSAudio.cpp,v 1.16 2003/04/25 21:22:52 joshk Exp $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/soundcard.h>

#ifndef AFMT_S16_NE	/* Portability (FreeBSD 4.7) */
# ifdef WORDS_BIGENDIAN
#  define AFMT_S16_NE AFMT_S16_BE
# else
#  define AFMT_S16_NE AFMT_S16_LE
# endif
#endif

#include <errno.h>

#include "thException.h"
#include "thAudio.h"
#include "thAudioBuffer.h"
#include "thOSSAudio.h"


/* XXX XXX XXX */
/* XXX: DO NOT PASS ioctl() shorts that are part of a structure as it will
   treat the pointer as a 32-bit integer and overwrite the next 16-bits of the
   next element of the structure. THIS IS BAD. */


/* null is a placeholder; to have wav output plugins and audio output plugins
   we must maintain the same number of arguments for interopability */
thOSSAudio::thOSSAudio(char *null, const thAudioFmt *afmt)
	throw(thIOException)
{
	int oss_channels = afmt->channels; /* OSS uses the value of 0 to indicate
					  1 channel, and 1 to indicate 2 
					  channels, so we must decrement the 
					  channel count */
	int format = afmt->format;

	memcpy(&fmt, afmt, sizeof(thAudioFmt));

	switch(afmt->bits) {
	case 8:
		fmt.format = AFMT_U8;
		break;
	case 16:
		fmt.format = AFMT_S16_NE;
		break;
	}

	if ((fd = open("/dev/dsp", O_WRONLY)) < 0) {
		throw errno;
	}

	if (ioctl(fd, SNDCTL_DSP_SETFMT, &format) == -1) {
		fprintf(stderr, "setfmt: /dev/dsp: %s\n", strerror(errno));
	}

	if (ioctl(fd, SNDCTL_DSP_CHANNELS, &oss_channels) == -1) {
		fprintf(stderr, "setchannels: /dev/dsp: %s\n", strerror(errno));
	}

	if (ioctl(fd, SNDCTL_DSP_SPEED, &fmt.samples) == -1) {
		fprintf(stderr, "setspeed: /dev/dsp: %s\n", strerror(errno));
	}	
}

thOSSAudio::~thOSSAudio()
{
	close(fd);
}

void thOSSAudio::SetFormat (const thAudioFmt *afmt)
{
	int oss_channels = afmt->channels; /* OSS uses the value of 0 to indicate
										  1 channel, and 1 to indicate 2 
										  channels, so we must decrement the 
										  channel count */
	int format = afmt->format;

	memcpy(&fmt, afmt, sizeof(thAudioFmt));

	switch(fmt.bits) {
	case 8:
		printf("setting audio format to u8\n");
		fmt.format = AFMT_U8;
		break;
	case 16:
		printf("setting audio format to s16ne\n");
		fmt.format = AFMT_S16_LE;
		break;
	}

	if(ioctl(fd, SNDCTL_DSP_SETFMT, &format) == -1) {
		fprintf(stderr, "/dev/dsp: %s\n", strerror(errno));
	}

	if(ioctl(fd, SNDCTL_DSP_CHANNELS, &oss_channels) == -1) {
		fprintf(stderr, "/dev/dsp: %s\n", strerror(errno));
	}

	if(ioctl(fd, SNDCTL_DSP_SPEED, &fmt.samples) == -1) {
		fprintf(stderr, "/dev/dsp: %s\n", strerror(errno));
	}
}

int thOSSAudio::Write (void *buf, int len)
{
	write(fd, buf, len);

	return 0;
}

int thOSSAudio::Read(void *buf, int len)
{
	return read(fd, buf, len);
}

void thOSSAudio::Play(thAudio *audioPtr)
{
	const thAudioFmt *afmt = audioPtr->GetFormat();
	int buf_size = afmt->samples;
	int r;

	SetFormat(afmt);
/*
	fcntl(fd, F_SETFL, O_NONBLOCK);
	ioctl(fd, SNDCTL_DSP_SYNC, 0);
*/

	printf("playing with bufsiz of %d\n", buf_size);

	switch(fmt.bits) {
	case 8:
	{
		unsigned char *buf = new unsigned char[buf_size];

		printf("playing 8-bit audio\n");

		while((r = audioPtr->Read(buf, buf_size)) > 0) {
			printf("writing %d bytes\n", r);
			Write(buf, r);
		}
		
		delete buf;
	}
	break;
	case 16:
	{
		signed short *buf = new signed short[buf_size];

		printf("playing 16-bit audio\n");

		while((r = audioPtr->Read(buf, buf_size)) > 0) {
			printf("writing %d bytes\n", r*2);
			Write(buf, r*2);
		}

		delete buf;
	}
	break;
	default:
		printf("unsupported bitsize: %d\n", fmt.bits);
		break;
	}

/*	
	ioctl(fd, SNDCTL_DSP_SYNC, 1);


	fcntl(fd, F_SETFL, 0);

	fd_set wfds;

	FD_ZERO(&wfds);
	FD_SET(fd, &wfds);
	
	while(1) {
		select(fd+1, NULL, &wfds, NULL, NULL);
		
		if(FD_ISSET(fd, &wfds)) {
			break;
		}
	}
*/
}
