/* $Id: thALSAAudio.h,v 1.1 2004/01/25 11:31:02 misha Exp $ */

#ifndef TH_ALSAAUDIO_H
#define TH_ALSAAUDIO_H

#include "thException.h"
#include <alsa/asoundlib.h>

//#define ALSA_BUFSIZE 512

class thALSAAudio : public thAudio
{
public:

	thALSAAudio (char *null, const thAudioFmt *afmt)
		throw(thIOException);

	virtual ~thALSAAudio ();

	void Play (thAudio *audioPtr);

	int Write (float *, int len);
	int Read (void *, int len);
	const thAudioFmt *GetFormat (void) { return &ofmt; };
	void SetFormat (const thAudioFmt *fmt);

	snd_pcm_t *play_handle;
private:
	thAudioFmt ofmt, ifmt;
	void *outbuf;
};

#endif /* TH_ALSAAUDIO_H */
