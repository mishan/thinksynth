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
	int cnt;

	for(cnt = 0; cnt < len; cnt++) {
		data[(read + woffset + cnt)%size] = udata[cnt];
	}

	woffset += len;
}

void AudioBuffer::buf_read(int *udata, int len)
{
	int cnt;

	for(cnt = 0; cnt < len; cnt++) {
		udata[cnt] = data[(read+cnt)%size];
	}

	read += len;
}

#if 0
int main (void)
{
	AudioBuffer *mybuf = new AudioBuffer(100);
	int tempbuf[30], i;
	int *readbuf;

	readbuf = (int *)alloca(10);

	for(i = 0; i < 30; i++) {
		tempbuf[i] = i;
	}

	for(i = 0; i < 10; i++) {
		while(mybuf->is_room(30)) {
			mybuf->buf_write(tempbuf, 30);
		}
		mybuf->buf_read(readbuf, 10);
		printf("%s\n", readbuf);
	}
}
#endif
