/* $Id: thAudio.h,v 1.2 2004/04/15 09:38:42 misha Exp $ */

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

	virtual bool ProcessEvents (void) = 0;
private:
};

#endif /* TH_AUDIO_H */
