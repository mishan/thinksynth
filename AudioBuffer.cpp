#include <stdlib.h>
#include <stdio.h>

#include "AudioBuffer.h"

AudioBuffer::AudioBuffer(int len)
{
	data = new int[size];
	read = 0;
	woffset = 0;     /* how far the writing is ahead of the reading */
	size = len;
}

AudioBuffer::~AudioBuffer(void)
{
	delete data;
}

bool AudioBuffer::is_room(int len)
{
	if(woffset + len > size) {
		return false;
	}

	return true;
}

void AudioBuffer::buf_write(int *udata, int len)
{
	int i;

	for(i = 0; i < len; i++) {
		data[(read + woffset + i)%size] = udata[i];
	}

	woffset += len;
}

void AudioBuffer::buf_read(int *udata, int len)
{
	int i;

	for(i = 0; i < len; i++) {
		udata[i] = data[(read+i)%size];
	}

	read += len;
}

#ifdef TEST
int main (void)
{
	AudioBuffer *mybuf = new AudioBuffer(100);
	int tempbuf[30], i;
	int *readbuf;

	readbuf = new int[10];

	for(i = 48; i < 78; i++) {
		tempbuf[i] = i;
	}

	for(i = 0; i < 10; i++) {
		while(mybuf->is_room(30)) {
			mybuf->buf_write(tempbuf, 30);
		}
		mybuf->buf_read(readbuf, 10);
		printf("%s\n", readbuf);
	}

	delete readbuf;
}
#endif
