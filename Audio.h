#ifndef HAVE_AUDIO_H
#define HAVE_AUDIO_H 1

class Audio
{
public:
	virtual int Read(void *data, int len) = 0;
private:
};

#endif
