/* $Id: thAudio.h,v 1.6 2003/09/15 23:17:06 brandon Exp $ */

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
	// changed on 9/15/03 by brandon lewis
	// all thAudio classes will work with floating point buffers
	// converting to integer internally based on format data
	virtual int Read(void *, int len) = 0;
	virtual int Write(float *, int len) = 0;

	virtual const thAudioFmt *GetFormat(void) = 0;

	virtual void SetFormat (const thAudioFmt *fmt) = 0;
private:
};

#endif /* TH_AUDIO_H */
