#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/soundcard.h>
#include <errno.h>

#include "Exception.h"
#include "Audio.h"
#include "AudioBuffer.h"
#include "OSSAudio.h"

/* null is a placeholder; to have wav output plugins and audio output plugins
   we must maintain the same number of arguments for interopability */
OSSAudio::OSSAudio(char *null, AudioFormat *afmt)
	throw(IOException)
{
	int oss_channels = afmt->channels-1; /* OSS uses the value of 0 to indicate
											1 channel, and 1 to indicate 2 
											channels, so we must decrement the 
											channel count */

	switch(afmt->bits) {
	case 8:
		afmt->format = AFMT_U8;
		break;
	case 16:
		afmt->format = AFMT_S16_NE;
		break;
	}

	memcpy(&fmt, afmt, sizeof(AudioFormat));

	if((fd = open("/dev/dsp", O_RDWR)) < 0) {
		throw errno;
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

OSSAudio::~OSSAudio()
{
	close(fd);
}

void OSSAudio::set_format(AudioFormat *afmt)
{
	int oss_channels = fmt.channels-1; /* OSS uses the value of 0 to indicate 
										   1 channel, and 1 to indicate 2 
										   channels, so we must decrement the 
										   channel count */

	memcpy(&fmt, afmt, sizeof(AudioFormat));

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

AudioFormat OSSAudio::get_format (void)
{
	return fmt;
}

void OSSAudio::write_audio (void *buf, int len)
{
	ioctl(fd, SNDCTL_DSP_SYNC, 0);
	write(fd, buf, len);
	ioctl(fd, SNDCTL_DSP_SYNC, 1);
}

int OSSAudio::read_audio(void *buf, int len)
{
	return read(fd, buf, len);
}

int OSSAudio::Read(void *buf, int len)
{
	return read_audio(buf, len);
}

void OSSAudio::play(AudioBuffer *buffer)
{
	unsigned char buf[BUF_SIZE];

	while(buffer->buf_read(buf, BUF_SIZE)) {
		write_audio(buf, BUF_SIZE);
	}
}
