/* $Id: thALSAAudio.h,v 1.2 2004/04/07 04:09:29 misha Exp $ */

#ifndef TH_ALSAAUDIO_H
#define TH_ALSAAUDIO_H

#include "thException.h"
#include <alsa/asoundlib.h>

//#define ALSA_BUFSIZE 512

#define ALSA_DEFAULT_DEVICE "hw:0"

/* additional arguments are usually bound to the callbacks of this signal */
typedef SigC::Signal0<void> sigReadyWrite_t;
typedef SigC::Signal1<int, snd_seq_t *> sigMidiEvent_t;


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
	sigMidiEvent_t signal_midi_event (void);

	bool ProcessEvents (void);

	snd_pcm_t *play_handle;
	snd_seq_t *seq_handle;
	int seq_nfds, nfds;
	struct pollfd *pfds;
protected:
	bool open_seq (void);

	thSynth *synth;
	sigReadyWrite_t m_sigReadyWrite;
	sigMidiEvent_t m_sigMidiEvent;
private:
	thAudioFmt ofmt, ifmt;
	void *outbuf;
};

#endif /* TH_ALSAAUDIO_H */
