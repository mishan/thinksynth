/* $Id: gthALSAAudio.h,v 1.1 2004/05/11 19:46:11 misha Exp $ */

#ifndef THF_ALSAAUDIO_H
#define THF_ALSAAUDIO_H

#include "thException.h"
#include <alsa/asoundlib.h>

//#define ALSA_BUFSIZE 512

#define ALSA_DEFAULT_DEVICE "hw:0"

/* additional arguments are usually bound to the callbacks of this signal */
typedef SigC::Signal0<void> sigReadyWrite_t;

class thfALSAAudio : public thfAudio, public SigC::Object
{
public:
	thfALSAAudio (thSynth *argsynth)
		throw(thIOException);
	thfALSAAudio (thSynth *argsynth, const char *device)
		throw(thIOException);

	virtual ~thfALSAAudio ();

	int Write (float *, int len);
	int Read (void *, int len);
	const thfAudioFmt *GetFormat (void) { return &ofmt; };
	void SetFormat (thSynth *argsynth);
	void SetFormat (const thfAudioFmt *afmt);

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
	thfAudioFmt ofmt, ifmt;
	void *outbuf;
};

#endif /* THF_ALSAAUDIO_H */
