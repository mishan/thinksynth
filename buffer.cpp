#include <stdlib.h>
#include <stdio.h>

#include "buffer.h"

Buffer *buffer_new(int size)
{
	Buffer *buf = new Buffer[1];

	buf->data = new int[size];
	buf->read = 0;
	buf->woffset = 0;     /* how far the writing is ahead of the reading */
	buf->len = size;

	return buf;
}

void buffer_free(Buffer *buf) {
	delete buf->data;
}

int buffer_is_room(Buffer *buf, int size) {
	if(buf->woffset + size > buf->len) { return(0); }
	return(1);
}

void buffer_write(Buffer *buf, int *data, int len) {
	int cnt;

	for(cnt=0;cnt<len;cnt++) {
		buf->data[(buf->read + buf->woffset + cnt)%buf->len] = data[cnt];
	}
	buf->woffset += len;
}

void buffer_read(Buffer *buf, int *data, int len) {
	int cnt;

	for(cnt=0;cnt<len;cnt++) {
		data[cnt] = buf->data[(buf->read+cnt)%buf->len];
	}
	buf->read += len;
}

void main(void) {
	Buffer *mybuf = buffer_new(100);
	int tempbuf[30], cnt;
	int *readbuf;

	readbuf = (int *)alloca(10);

	for(cnt=0;cnt<30;cnt++) {
		tempbuf[cnt] = cnt;
	}

	for(cnt=0;cnt<10;cnt++) {
		while(buffer_is_room(mybuf, 30)) {
			buffer_write(mybuf, tempbuf, 30);
		}
		buffer_read(mybuf, readbuf, 10);
		printf("%s\n", readbuf);
	}
}
