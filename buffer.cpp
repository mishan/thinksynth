#include <stdlib.h>
#include <stdio.h>

#include "buffer.h"

buffer *buffer_new(int size) {
	buffer *buf = malloc(sizeof(buffer));

	buf->data = malloc(size*sizeof(int));
	buf->read = 0;
	buf->woffset = 0;     /* how far the writing is ahead of the reading */
	buf->len = size;
}

void buffer_free(buffer *buf) {
	free(buf->data);
}

int buffer_is_room(buffer *buf, int size) {
	if(buf->woffset + size > buf->len) { return(0); }
	return(1);
}

void buffer_write(buffer *buf, int *data, int len) {
	int cnt;

	for(cnt=0;cnt<len;cnt++) {
		buf->data[(buf->read + buf->woffset + cnt)%buf->len] = buf[cnt];
	}
	buf->woffset += len;
}

void buffer_read(buffer *buf, int *data, int len) {
	int cnt;

	for(cnt=0;cnt<len;cnt++) {
		data[cnt] = buf->data[(buf->read+cnt)%buf->len];
	}
	buf->read += len;
}

void main(void) {
	buffer *mybuf = buffer_new(100);
	int tempbuf[30], cnt;
	int *readbuf;

	readbuf = alloca(10);

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
