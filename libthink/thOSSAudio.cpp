/* $Id: thOSSAudio.cpp,v 1.23 2003/10/16 21:04:03 misha Exp $ */

#include "config.h"
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
#include "thEndian.h"

#include "think.h"
/* XXX XXX XXX */
/* XXX: DO NOT PASS ioctl() shorts that are part of a structure as it will
   treat the pointer as a 32-bit integer and overwrite the next 16-bits of the
   next element of the structure. THIS IS BAD. */


/* null is a placeholder; to have wav output plugins and audio output plugins
   we must maintain the same number of arguments for interopability */
thOSSAudio::thOSSAudio(char *null, const thAudioFmt *afmt)
	throw(thIOException)
{

	/* SetFormat closes and reopens the /dev/dsp device
	   before changing settings, as is recomended in the
	   OSS API Spec. here we open fd to make sure calling
	   SetFormat causes no problems */
	if ((fd = open("/dev/dsp", O_WRONLY)) < 0) {
		throw errno;
	}

	SetFormat(afmt);

	/* this will be allocated upon the first call to Write() */
	outbuf = NULL;
}

thOSSAudio::~thOSSAudio()
{
	if (outbuf) {
		free(outbuf);
	}

	close(fd);
}

void thOSSAudio::SetFormat (const thAudioFmt *afmt)
{
	fprintf(stderr,"thOSSAudio::SetFormat()--initializing sound device\n");
	/* copy format data to local structure */
	memcpy(&ifmt, afmt, sizeof(thAudioFmt));

	/* override value in format field with the bits parameter */
	switch(afmt->bits) {
	case 8:
		ifmt.format = AFMT_U8;
		break;
	case 16:
		ifmt.format = AFMT_S16_NE;
		break;
	}

	ifmt.channels = afmt->channels;

	/* instantiate local copies of data stored in format structure to avoid 
	   problems with calls to ioctl -- see above */

	int oss_format = ifmt.format;
	int oss_channels = ifmt.channels;
	int oss_samples = ifmt.samples;

	/* XXX: this should be done inside a separate method */
	/* close /dev/dsp before changing settings even calling SNDCTL_DSP_RESET is
	   not recomended */
 	close (fd);

	if ((fd = open("/dev/dsp", O_WRONLY)) < 0) {
		throw errno;
	}

	fprintf(stderr,"\tthOSSAudio: trying %d-bit sample width\n",oss_format);
	if (ioctl(fd, SNDCTL_DSP_SETFMT, &oss_format) == -1) {
		fprintf(stderr, "setfmt: /dev/dsp: %s\n", strerror(errno));
	}
	fprintf(stderr,"\t\tusing %d-bit sample width\n",oss_format);

	fprintf(stderr,"\tthOSSAudio: trying %d channels\n",oss_channels);
	if (ioctl(fd, SNDCTL_DSP_CHANNELS, &oss_channels) == -1) {
		fprintf(stderr, "setchannels: /dev/dsp: %s\n", strerror(errno));
	}
	fprintf(stderr,"\t\tusing %d channels\n",oss_channels);

	fprintf(stderr,"\tthOSSAudio: trying %d samples per second\n",oss_samples);
	if (ioctl(fd, SNDCTL_DSP_SPEED, &oss_samples) == -1) {
		fprintf(stderr, "setspeed: /dev/dsp: %s\n", strerror(errno));
	}
	fprintf(stderr,"\t\tusing %d samples per second\n",oss_samples);

	/* since a sound card may not support the required formats store the 
	   returned values in the ofmt structure so the application can decide what
	   to do with the sound data */

	ofmt.format = oss_format;
	ofmt.channels = oss_channels;
	ofmt.samples = oss_samples;

	ofmt.bits = oss_format; /* XXX: this is bad, change this */
}

int thOSSAudio::Write (float *inbuf, int len)
{
	int i ,w = 0;
	int chans = ofmt.channels;
	int bytes = ofmt.bits / 8;
	int samplelen = bytes*chans;

	/* malloc an appropriate buffer it would be *bad* if the length of the 
	   buffer passed in were to increase so don't do that (brandon) */
	if (!outbuf){
		outbuf = malloc(len*bytes);
		if (!outbuf){
			fprintf(stderr,"thOSSAudio::Write -- could not allocate buffer\n");
			exit(1);
		}
	}
	if (bytes == 2) {
		signed short *buf = (signed short*)outbuf;
		/* convert to specified format */
		for (i = 0; i < len; i++){ 
			le16(buf[i],(signed short)(((float)inbuf[i]/TH_MAX)*32767));
		}
	}
	else {
		fprintf(stderr,"\tthOSSAudio::Write() error: %d-bit audio unsupported!\n",ofmt.bits);
		exit(1);
	}

	/* XXX: WTF? */
	unsigned char *buff = (unsigned char *)outbuf;

	w = write(fd, buff, len*bytes);

	return w;
}

int thOSSAudio::Read(void *inbuf, int len)
{
	/* XXX: reading in of sound is NOT implemented */
	/*	return read(fd, outbuf, len); */
	return -1;
}

/* XXX: NOT IMPLEMENTED */
void thOSSAudio::Play(thAudio *audioPtr)
{
/*	const thAudioFmt *afmt = audioPtr->GetFormat();
	int buf_size = afmt->samples;
	int r;

	SetFormat(afmt);
*/
/*
	fcntl(fd, F_SETFL, O_NONBLOCK);
	ioctl(fd, SNDCTL_DSP_SYNC, 0);
*/
/*
	printf("playing with bufsiz of %d\n", buf_size);

	switch(ofmt.bits) {
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
		printf("unsupported bitsize: %d\n", ofmt.bits);
		break;
	}
*/
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
