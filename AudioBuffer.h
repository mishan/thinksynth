#ifndef HAVE_BUFFER_H
#define HAVE_BUFFER_H 1

class AudioBuffer 
{
public:
	AudioBuffer(int len);
	~AudioBuffer(void);

	bool is_room(int len); /* checks if there are len elements available in the
							  buffer */
	void buf_write(int *udata, int len);
	void buf_read(int *udata, int len);
private:
	int *data;
	int read; /* how far the buffer has been read */
	int woffset; /* how far the writing is ahead of the reading */
	int size; /* the length of the buffer */
};

#endif /* HAVE_BUFFER_H */
