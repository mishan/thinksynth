/* $Id: thALSAAudio.h,v 1.3 2004/04/15 09:38:42 misha Exp $ */

#ifndef TH_ALSAAUDIO_H
#define TH_ALSAAUDIO_H

#include "thException.h"
#include <alsa/asoundlib.h>

//#define ALSA_BUFSIZE 512

#define ALSA_DEFAULT_DEVICE "hw:0"

/* additional arguments are usually bound to the callbacks of this signal */
typedef SigC::Signal0<void> sigReadyWrite_t;

class thALSAAudio : public thAudio
{
public:
	thALSAAudio (thSynth *argsynth)
		throw(thIOException);
	thALSAAudio (thSynth *argsynth, const char *device)
		throw(thIOException);

	virtual ~thALSAAudio ();

	void Play (thAudio *audioPtr);

	int Write (float *, int len);
	int Read (void *, int len);
	const thAudioFmt *GetFormat (void) { return &ofmt; };
	void SetFormat (thSynth *argsynth);
	void SetFormat (const thAudioFmt *afmt);

	sigReadyWrite_t signal_ready_write (void);

	bool ProcessEvents (void);

	snd_pcm_t *play_handle;
	int nfds;
	struct pollfd *pfds;
protected:
	thSynth *synth;
	sigReadyWrite_t m_sigReadyWrite;
private:
	thAudioFmt ofmt, ifmt;
	void *outbuf;
};

#endif /* TH_ALSAAUDIO_H */
