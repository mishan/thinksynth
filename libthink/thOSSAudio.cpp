/* $Id: thOSSAudio.cpp,v 1.20 2003/09/14 20:43:19 ink Exp $ */

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

/* XXX XXX XXX */
/* XXX: DO NOT PASS ioctl() shorts that are part of a structure as it will
   treat the pointer as a 32-bit integer and overwrite the next 16-bits of the
   next element of the structure. THIS IS BAD. */


/* null is a placeholder; to have wav output plugins and audio output plugins
   we must maintain the same number of arguments for interopability */
thOSSAudio::thOSSAudio(char *null, const thAudioFmt *afmt)
	throw(thIOException)
{
	fprintf(stderr,"thOSSAudio()--initializing sound device\n");
	/* copy format data to local structure */
	memcpy(&fmt, afmt, sizeof(thAudioFmt));

	/* override value in format field with the bits parameter */
	/* just a thought; could one of thse parameters be redundant? */

	switch(afmt->bits) {
	case 8:
		fmt.format = AFMT_U8;
		break;
	case 16:
		fmt.format = AFMT_S16_NE;
		break;
	}


	/* OSS uses the value of 0 to indicate 1 channel,
	and 1 to indicate 2 channels, so we must decrement
	the channel count */

	/* note that the above does not appear to be true any longer */
	/* i'm not decrementing this value */

	fmt.channels = afmt->channels;

	/* instantiate local copies of data stored in format structure*/
	/* to avoid problems with calls to ioctl -- see above */

	int oss_format = fmt.format;
	int oss_channels = fmt.channels;
	int oss_samples = fmt.samples;

	if ((fd = open("/dev/dsp", O_WRONLY)) < 0) {
		throw errno;
	}

	fprintf(stderr,"\tTHOSSAUDIO: trying %d-bit sample width\n",oss_format);
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

	/* since a sound card may not support the required formats
	store the returned values in the fmt structure so the application
	can decide what to do with the sound data*/

	fmt.format = oss_format;
	fmt.channels = oss_channels;
	fmt.samples = oss_samples;
}

thOSSAudio::~thOSSAudio()
{
	close(fd);
}

void thOSSAudio::SetFormat (const thAudioFmt *afmt)
{
	fprintf(stderr,"thOSSAudio()--initializing sound device\n");
	/* copy format data to local structure */
	memcpy(&fmt, afmt, sizeof(thAudioFmt));

	/* override value in format field with the bits parameter */
	/* just a thought; could one of thse parameters be redundant? */

	switch(afmt->bits) {
	case 8:
		fmt.format = AFMT_U8;
		break;
	case 16:
		fmt.format = AFMT_S16_NE;
		break;
	}


	/* OSS uses the value of 0 to indicate 1 channel,
	and 1 to indicate 2 channels, so we must decrement
	the channel count */

	/* note that the above does not appear to be true any longer */
	/* i'm not decrementing this value */

	fmt.channels = afmt->channels;

	/* instantiate local copies of data stored in format structure*/
	/* to avoid problems with calls to ioctl -- see above */

	int oss_format = fmt.format;
	int oss_channels = fmt.channels;
	int oss_samples = fmt.samples;

	// the OSS specification recomends closing and reopening the driver
	// as opposed to using the SNDCTL_DSP_RESET ioctl call

	close(fd);
	if ((fd = open("/dev/dsp", O_WRONLY)) < 0) {
		throw errno;
	}

	fprintf(stderr,"\tTHOSSAUDIO: trying %d-bit sample width\n",oss_format);
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

	/* since a sound card may not support the required formats
	store the returned values in the fmt structure so the application
	can decide what to do with the sound data*/

	fmt.format = oss_format;
	fmt.channels = oss_channels;
	fmt.samples = oss_samples;
}

int thOSSAudio::Write (void *buf, int len)
{
	int i;
	int chans = fmt.channels;
	int bytes = fmt.bits / 8;
	int samplelen = bytes*chans;

	/* must write to sound card in multiple of 4 bytes */
	/* i.e. if the sample is 16-bit stereo, a complete */
	/* sample is 4 bytes...for 16-bit mono, only two   */
	/* bytes would be necessary...			   */
	/* don't know why this code actually works, as the */
	/* as the buffer length is a multiple of 1024 in   */
	/* the first place */

	if ( bytes == 2){
		unsigned short *buff = (unsigned short *)buf;
		for (i = 0; i < len; i+=chans*1024){
			write(fd,&buff[i],samplelen*1024);
		}
		return 0;
	}

	else {
		fprintf(stderr,"\tthOSSAudio::Write() error: %d-bit audio unsupported!\n",fmt.bits);
		exit(0);
	}
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
