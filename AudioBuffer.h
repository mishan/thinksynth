#ifndef HAVE_BUFFER_H
#define HAVE_BUFFER_H 1

class AudioBuffer 
{
public:
	AudioBuffer(int len);
	~AudioBuffer(void);

	bool is_room(int len);
	void buf_write(int *udata, int len);
	void buf_read(int *udata, int len);
private:
	int *data;
	int read, woffset, size;
};

#endif /* HAVE_BUFFER_H */
