/* $Id: thAudio.h,v 1.5 2003/05/07 07:39:45 aaronl Exp $ */

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
	virtual int Read(void *data, int len) = 0;
	virtual int Write(void *data, int len) = 0;

	virtual const thAudioFmt *GetFormat(void) = 0;

	virtual void SetFormat (const thAudioFmt *fmt) = 0;
private:
};

#endif /* TH_AUDIO_H */
