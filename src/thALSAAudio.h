/* $Id: thALSAAudio.h,v 1.5 2004/05/04 04:05:59 misha Exp $ */

#ifndef TH_ALSAAUDIO_H
#define TH_ALSAAUDIO_H

#include "thException.h"
#include <alsa/asoundlib.h>

//#define ALSA_BUFSIZE 512

#define ALSA_DEFAULT_DEVICE "hw:0"

/* additional arguments are usually bound to the callbacks of this signal */
typedef SigC::Signal0<void> sigReadyWrite_t;

class thALSAAudio : public thAudio, public SigC::Object
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

	bool pollAudioEvent (Glib::IOCondition);
protected:
	thSynth *synth;
	sigReadyWrite_t m_sigReadyWrite;

	void main (void);
private:
	Glib::Thread *thread;
	thAudioFmt ofmt, ifmt;
	void *outbuf;
};

#endif /* TH_ALSAAUDIO_H */
