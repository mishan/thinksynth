/* $Id: thAudio.h,v 1.7 2003/11/04 06:59:11 misha Exp $ */

#ifndef TH_AUDIO_H
#define TH_AUDIO_H 1

struct thAudioFmt {
	short format;
	short channels;
	short bits;

	int samples;
	int len;
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
