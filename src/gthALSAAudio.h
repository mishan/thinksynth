/* $Id: gthALSAAudio.h,v 1.2 2004/06/30 03:47:45 misha Exp $ */

#ifndef GTH_ALSAAUDIO_H
#define GTH_ALSAAUDIO_H

#include "thException.h"
#include <alsa/asoundlib.h>

//#define ALSA_BUFSIZE 512

#define ALSA_DEFAULT_DEVICE "hw:0"

/* additional arguments are usually bound to the callbacks of this signal */
typedef SigC::Signal0<void> sigReadyWrite_t;

class gthALSAAudio : public gthAudio, public SigC::Object
{
public:
	gthALSAAudio (thSynth *argsynth)
		throw(thIOException);
	gthALSAAudio (thSynth *argsynth, const char *device)
		throw(thIOException);

	virtual ~gthALSAAudio ();

	int Write (float *, int len);
	int Read (void *, int len);
	const gthAudioFmt *GetFormat (void) { return &ofmt; };
	void SetFormat (thSynth *argsynth);
	void SetFormat (const gthAudioFmt *afmt);

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
	gthAudioFmt ofmt, ifmt;
	void *outbuf;
};

#endif /* GTH_ALSAAUDIO_H */
