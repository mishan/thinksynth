#ifndef HAVE_BUFFER_H
#define HAVE_BUFFER_H 1

class AudioBuffer 
{
public:
	AudioBuffer(int len, Audio *audio);
	~AudioBuffer(void);

	bool is_room(int len); /* checks if there are len elements available in the
							  buffer */
	void buf_write(unsigned char *udata, int len);
	int buf_read(unsigned char *udata, int len);
private:
	unsigned char *data;
	int read; /* how far the buffer has been read */
	int woffset; /* how far the writing is ahead of the reading */
	int size; /* the length of the buffer */
//	int (*read_fn)(void *data, int len);
	Audio *audioPtr;
};

#endif /* HAVE_BUFFER_H */
