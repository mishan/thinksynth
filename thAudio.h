#ifndef HAVE_TH_AUDIO_H
#define HAVE_TH_AUDIO_H 1

struct thAudioFmt {
	short format;
	short channels;
	short bits;
	
	long samples;
	long len;
};

class thAudio
{
public:
	virtual int Read(void *data, int len) = 0;
	virtual int Write(void *data, int len) = 0;

	virtual const thAudioFmt *GetFormat(void) = 0;

	virtual void SetFormat (const thAudioFmt *fmt) = 0;
private:
};

#endif /* HAVE_TH_AUDIO_H */
