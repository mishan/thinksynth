#include <stdio.h>
#include <stdlib.h>

#include "thAudio.h"
#include "thAudioBuffer.h"

thAudioBuffer::thAudioBuffer(int len, thAudio *audio)
{
	size = len;
	data = new unsigned char[size];
	read = 0;
	woffset = 0;     /* how far the writing is ahead of the reading */
	audioPtr = audio;
}

thAudioBuffer::~thAudioBuffer(void)
{
	delete data;
}

bool thAudioBuffer::is_room(int len)
{
	if(woffset + len > size) {
		return false;
	}

	return true;
}

void thAudioBuffer::buf_write(unsigned char *udata, int len)
{
	int i;

	for(i = 0; i < len; i++) {
		data[(read + woffset + i)%size] = udata[i];
	}

	woffset += len;
}

int thAudioBuffer::buf_read(unsigned char *udata, int len)
{
	int i, bufempty = (int)(size*BUFFER_EMPTY_PERCENT);
	unsigned char *buf = new unsigned char[bufempty];

	printf("=-= %i %i =-=\n", woffset, len);
	while(woffset <= bufempty) {
		//printf("-= %i %i %i =-\n", woffset, size, bufempty);

		if((audioPtr->Read(buf, bufempty) <= 0)) {
			printf("--- EOF ---\n");
			return -1;
		}

		buf_write(buf, bufempty);
	}

	delete buf;

	for(i = 0; i < len; i++) {
		udata[i] = data[(read+i)%size];
	}

	read += len;
	woffset -= len;
	read %= size;
	
	return len;
}

int thAudioBuffer::get_size(void)
{
	return size;
}

#ifdef TEST
int main (void)
{
	thAudioBuffer *mybuf = new thAudioBuffer(100);
	int tempbuf[30], i;
	int *readbuf;

	readbuf = new int[10];

	for(i = 0; i < 30; i++) {
		tempbuf[i] = i+48;
	}

	for(i = 0; i < 10; i++) {
		while(mybuf->is_room(30)) {
			mybuf->buf_write(tempbuf, 30);
		}
		mybuf->buf_read(readbuf, 10);
		printf("%s\n", (char *)readbuf);
	}

	delete mybuf;
	delete readbuf;
}
#endif
