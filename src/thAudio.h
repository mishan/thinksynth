/* $Id: thAudio.h,v 1.1 2004/04/06 19:03:55 misha Exp $ */

#ifndef TH_AUDIO_H
#define TH_AUDIO_H 1

struct thAudioFmt {
	short format;
	short channels;
	short bits;

	int samples;
	int len;
	int period;
};

class thAudio
{
public:
	inline virtual ~thAudio () {
	}

	virtual int Read(void *, int len) = 0;
	virtual int Write(float *, int len) = 0;

	virtual const thAudioFmt *GetFormat(void) = 0;

	virtual void SetFormat (const thAudioFmt *fmt) = 0;
private:
};

#endif /* TH_AUDIO_H */
