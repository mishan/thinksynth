/* $Id: gthAudio.h,v 1.1 2004/05/11 19:51:38 misha Exp $ */

#ifndef THF_AUDIO_H
#define THF_AUDIO_H 1

struct thfAudioFmt {
	short format;
	short channels;
	short bits;

	int samples;
	int len;
	int period;
};

class thfAudio
{
public:
	inline virtual ~thfAudio () {
	}

	virtual int Read(void *, int len) = 0;
	virtual int Write(float *, int len) = 0;

	virtual const thfAudioFmt *GetFormat(void) = 0;

	virtual void SetFormat (const thfAudioFmt *fmt) = 0;

	virtual bool ProcessEvents (void) = 0;
private:
};

#endif /* THF_AUDIO_H */
