#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/soundcard.h>
#include <errno.h>

#include "thException.h"
#include "thAudio.h"
#include "thAudioBuffer.h"
#include "thOSSAudio.h"

/* null is a placeholder; to have wav output plugins and audio output plugins
   we must maintain the same number of arguments for interopability */
thOSSAudio::thOSSAudio(char *null, const thAudioFmt *afmt)
	throw(thIOException)
{
	int oss_channels = afmt->channels-1; /* OSS uses the value of 0 to indicate
											1 channel, and 1 to indicate 2 
											channels, so we must decrement the 
											channel count */

	memcpy(&fmt, afmt, sizeof(thAudioFmt));

	switch(afmt->bits) {
	case 8:
		fmt.format = AFMT_S8;
		break;
	case 16:
		fmt.format = AFMT_S16_NE;
		break;
	}

	if((fd = open("/dev/dsp", O_WRONLY)) < 0) {
		throw errno;
	}

	if(ioctl(fd, SNDCTL_DSP_SETFMT, &fmt.format) == -1) {
		fprintf(stderr, "setfmt: /dev/dsp: %s\n", strerror(errno));
	}

	if(ioctl(fd, SNDCTL_DSP_STEREO, &oss_channels) == -1) {
		fprintf(stderr, "setchannels: /dev/dsp: %s\n", strerror(errno));
	}

	if(ioctl(fd, SNDCTL_DSP_SPEED, &fmt.samples) == -1) {
		fprintf(stderr, "setspeed: /dev/dsp: %s\n", strerror(errno));
	}	
}

thOSSAudio::~thOSSAudio()
{
	close(fd);
}

void thOSSAudio::SetFormat (const thAudioFmt *afmt)
{
	int oss_channels = afmt->channels-1; /* OSS uses the value of 0 to indicate 											1 channel, and 1 to indicate 2 
											channels, so we must decrement the 
											channel count */
	
	printf("setting %d channels\n", oss_channels);

	memcpy(&fmt, afmt, sizeof(thAudioFmt));

	switch(afmt->bits) {
	case 8:
		printf("setting audio format to u8\n");
		fmt.format = AFMT_U8;
		break;
	case 16:
		printf("setting audio format to s16ne\n");
		fmt.format = AFMT_S16_NE;
		break;
	}

	if(ioctl(fd, SNDCTL_DSP_SETFMT, &fmt.format) == -1) {
		fprintf(stderr, "/dev/dsp: %s\n", strerror(errno));
	}

	if(ioctl(fd, SNDCTL_DSP_STEREO, &oss_channels) == -1) {
		fprintf(stderr, "/dev/dsp: %s\n", strerror(errno));
	}

	if(ioctl(fd, SNDCTL_DSP_SPEED, &fmt.samples) == -1) {
		fprintf(stderr, "/dev/dsp: %s\n", strerror(errno));
	}
}

const thAudioFmt *thOSSAudio::GetFormat (void)
{
	return &fmt;
}

int thOSSAudio::Write (void *buf, int len)
{
	ioctl(fd, SNDCTL_DSP_SYNC, 0);
	write(fd, buf, len);
	ioctl(fd, SNDCTL_DSP_SYNC, 1);

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

	fcntl(fd, F_SETFL, O_NONBLOCK);

	printf("playing with bufsiz of %d\n", buf_size);

	switch(fmt.bits) {
	case 8:
	{
		unsigned char buf[buf_size];

		printf("playing 8-bit audio\n");

		while((r = audioPtr->Read(buf, buf_size)) > 0) {
			printf("writing %d bytes\n", r);
			Write(buf, r);
		}
	}
	break;
	case 16:
	{
		signed short buf[buf_size];

		printf("playing 16-bit audio\n");

		while((r = audioPtr->Read(buf, buf_size)) > 0) {
			printf("writing %d bytes\n", r*2);
			Write(buf, r*2);
		}
	}
	break;
	}

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
}
