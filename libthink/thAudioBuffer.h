/* $Id: thAudioBuffer.h,v 1.7 2003/04/27 05:23:09 joshk Exp $ */

#ifndef TH_AUDIOBUFFER_H
#define TH_AUDIOBUFFER_H 1

#define BUFFER_EMPTY_PERCENT 0.6

class thAudioBuffer 
{
public:
	thAudioBuffer(int len, thAudio *audio);
	~thAudioBuffer(void);

	bool is_room(int len) const { return (woffset + len > size); };
	/* checks if there are len elements available in the buffer */

	int get_size(void) const { return (size); };

	void buf_write(unsigned char *udata, int len);
	int buf_read(unsigned char *udata, int len);

private:
	unsigned char *data;
	int read; /* how far the buffer has been read */
	int woffset; /* how far the writing is ahead of the reading */
	int size; /* the length of the buffer */
/*	int (*read_fn)(void *data, int len); */
	thAudio *audioPtr;
};

#endif /* TH_AUDIOBUFFER_H */
