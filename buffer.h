typedef struct buffer {
	int *data;
	int read, woffset, len;
};

buffer *buffer_new(int size);
void buffer_free(buffer *buf);
int buffer_is_room(buffer *buf, int size);
void buffer_write(buffer *buf, int *data, int len);
void buffer_read(buffer *buf, int *data, int len)
