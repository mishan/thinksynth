/* $Id: gthAudio.h,v 1.2 2004/06/30 03:47:45 misha Exp $ */

#ifndef GTH_AUDIO_H
#define GTH_AUDIO_H 1

struct gthAudioFmt {
	short format;
	short channels;
	short bits;

	int samples;
	int len;
	int period;
};

class gthAudio
{
public:
	inline virtual ~gthAudio () {
	}

	virtual int Read(void *, int len) = 0;
	virtual int Write(float *, int len) = 0;

	virtual const gthAudioFmt *GetFormat(void) = 0;

	virtual void SetFormat (const gthAudioFmt *fmt) = 0;

	virtual bool ProcessEvents (void) = 0;
private:
};

#endif /* GTH_AUDIO_H */
