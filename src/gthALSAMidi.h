/* $Id: gthALSAMidi.h,v 1.1 2004/04/15 07:41:17 misha Exp $ */

#ifndef THF_ALSAMIDI_H
#define THF_ALSAMIDI_H

#define ALSA_DEFAULT_DEVICE "hw:0"

typedef SigC::Signal1<int, snd_seq_t *> sigMidiEvent_t;

class thfALSAMidi
{
public:
	thfALSAMidi (const char *argname)
		throw(thIOException);
	~thfALSAMidi (void);

	sigMidiEvent_t signal_midi_event (void);

	bool ProcessEvents (void);

protected:
	string name, device;

	bool open_seq (void);

	snd_seq_t *seq_handle;
	int seq_nfds;
	struct pollfd *pfds;

	sigMidiEvent_t m_sigMidiEvent;
};

#endif /* THF_ALSAMIDI_H */
