#ifndef HAVE_OSSAUDIO_H
#define HAVE_OSSAUDIO_H

#include <sys/soundcard.h>

#define DEFAULT_FREQ 44100
#define DEFAULT_FORMAT AFMT_U8
#define DEFAULT_CHANNELS 2
#define DEFAULT_SAMPLES 4096

#define BUF_SIZE 65536
#define BUF_REFILL_PERCENT 0.5

struct AudioFormat {
	int format;
	int channels;
	int samples;
	int bits;
};

class OSSAudio: public Audio
{
public:
	OSSAudio(char *null, AudioFormat *fmt)
		throw(thIOException);

	virtual ~OSSAudio();

	void Play (AudioBuffer *buffer);
	void Write (void *stream, int len);
	int Read (void *data, int len);

	AudioFormat GetFormat (void);

	void SetFormat (AudioFormat *fmt);
private:
	int fd;
	AudioFormat fmt;
};

inline OSSAudio *new_OSSAudio(char *null, AudioFormat *afmt)
{
	try {
		OSSAudio *audio = new OSSAudio(null, afmt);

		return audio;
	}
	catch (thIOException e) {
		fprintf(stderr, "OSSAudio::OSSAudio: %s\n", strerror(e));	
	}

	return NULL;
}

#endif /* HAVE_OSSAUDIO_H */
