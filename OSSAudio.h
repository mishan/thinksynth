#ifndef HAVE_AUDIO_H
#define HAVE_AUDIO_H

#include <sys/soundcard.h>

#define DEFAULT_FREQ 44100
#define DEFAULT_FORMAT AFMT_U8
#define DEFAULT_CHANNELS 2
#define DEFAULT_SAMPLES 4096

#define BUF_SIZE 16384

struct AudioFormat {
	int format;
	int channels;
	int samples;
	int bits;
};

class OSSAudio
{
public:
	OSSAudio(char *null, AudioFormat *fmt);

	~OSSAudio();

	int open_audio(void);
	void write_audio(void *stream, int len);
	void set_format(AudioFormat *fmt);
	AudioFormat get_format(void);
private:
	int fd;
	AudioFormat fmt;

	void close_audio(void);
};


#endif /* HAVE_AUDIO_H */
