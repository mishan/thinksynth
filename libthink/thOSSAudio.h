/* $Id */

#ifndef TH_OSSAUDIO_H
#define TH_OSSAUDIO_H

#include <sys/soundcard.h>

#define DEFAULT_FREQ 44100
#define DEFAULT_FORMAT AFMT_U8
#define DEFAULT_CHANNELS 2
#define DEFAULT_SAMPLES 4096

#define BUF_SIZE 65536
#define BUF_REFILL_PERCENT 0.5

class thOSSAudio: public thAudio
{
public:
	thOSSAudio(char *null, const thAudioFmt *afmt)
		throw(thIOException);

	virtual ~thOSSAudio();

	void Play (thAudio *audioPtr);
	int Write (void *stream, int len);
	int Read (void *data, int len);

	const thAudioFmt *GetFormat (void) { return &fmt; };

	void SetFormat (const thAudioFmt *fmt);
private:
	int fd;
	thAudioFmt fmt;
};

inline thOSSAudio *new_thOSSAudio(char *null, const thAudioFmt *afmt)
{
	try {
		thOSSAudio *audio = new thOSSAudio(null, afmt);

		return audio;
	}
	catch (thIOException e) {
		fprintf(stderr, "thOSSAudio::thOSSAudio: %s\n", strerror(e));	
	}

	return NULL;
}

#endif /* TH_OSSAUDIO_H */
