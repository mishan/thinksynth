#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/soundcard.h>
#include <errno.h>
#include "AudioBuffer.h"
#include "OSSAudio.h"

/* null is a placeholder; to have wav output plugins and audio output plugins
   we must maintain the same number of arguments for interopability */
OSSAudio::OSSAudio(char *null, AudioFormat *fmt)
{
	switch(fmt->bits) {
	case 8:
		fmt->format = AFMT_U8;
		break;
	case 16:
		fmt->format = AFMT_S16_NE;
		break;
	}

	memcpy(&this->fmt, fmt, sizeof(AudioFormat));
}

OSSAudio::~OSSAudio()
{
	close_audio();
}

int OSSAudio::open_audio(void)
{
	int oss_channels = fmt.channels-1; /* OSS uses the value of 0 to indicate 
										  1 channel, and 1 to indicate 2 
										  channels, so we must decrement the 
										  channel count */

	if((fd = open("/dev/dsp", O_RDWR)) < 0) {
		fprintf(stderr, "OSSAudio::OSSAudio: /dev/dsp: %s\n", strerror(errno));
		return -1;
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

	return 0;
}

void OSSAudio::set_format(AudioFormat *fmt)
{
	int oss_channels = fmt->channels-1; /* OSS uses the value of 0 to indicate 
										   1 channel, and 1 to indicate 2 
										   channels, so we must decrement the 
										   channel count */

	memcpy(&this->fmt, fmt, sizeof(AudioFormat));

	if(ioctl(fd, SNDCTL_DSP_SETFMT, &fmt->format) == -1) {
		fprintf(stderr, "/dev/dsp: %s\n", strerror(errno));
	}

	if(ioctl(fd, SNDCTL_DSP_STEREO, &oss_channels) == -1) {
		fprintf(stderr, "/dev/dsp: %s\n", strerror(errno));
	}

	if(ioctl(fd, SNDCTL_DSP_SPEED, &fmt->samples) == -1) {
		fprintf(stderr, "/dev/dsp: %s\n", strerror(errno));
	}
}

AudioFormat OSSAudio::get_format (void)
{
	return fmt;
}

void OSSAudio::close_audio(void)
{
	close(fd);
	
	fd = -1;
}

void OSSAudio::write_audio (void *buf, int len)
{
	ioctl(fd, SNDCTL_DSP_SYNC, 0);
	write(fd, buf, len);
	ioctl(fd, SNDCTL_DSP_SYNC, 1);
}
