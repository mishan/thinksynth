/* $Id: gthALSAMidi.h,v 1.2 2004/04/17 23:01:34 misha Exp $ */

#ifndef THF_ALSAMIDI_H
#define THF_ALSAMIDI_H

#define ALSA_DEFAULT_DEVICE "hw:0"

typedef SigC::Signal1<int, snd_seq_t *> sigMidiEvent_t;

class thfALSAMidi : public SigC::Object
{
public:
	thfALSAMidi (const char *argname)
		throw(thIOException);
	~thfALSAMidi (void);

	sigMidiEvent_t signal_midi_event (void);

	bool ProcessEvents (void);
	bool pollMidiEvent (Glib::IOCondition);
protected:
	string name, device;

	bool open_seq (void);

	snd_seq_t *seq_handle;
	int seq_nfds;
	struct pollfd *pfds;

	sigMidiEvent_t m_sigMidiEvent;
};

#endif /* THF_ALSAMIDI_H */
