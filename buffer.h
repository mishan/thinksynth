struct Buffer {
	int *data;
	int read, woffset, len;
};

extern Buffer *buffer_new(int size);
extern void buffer_free(Buffer *buf);
extern int buffer_is_room(Buffer *buf, int size);
extern void buffer_write(Buffer *buf, int *data, int len);
extern void buffer_read(Buffer *buf, int *data, int len);
