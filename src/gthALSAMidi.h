/* $Id: gthALSAMidi.h,v 1.3 2004/06/30 03:47:45 misha Exp $ */

#ifndef GTH_ALSAMIDI_H
#define GTH_ALSAMIDI_H

#define ALSA_DEFAULT_DEVICE "hw:0"

typedef SigC::Signal1<int, snd_seq_t *> sigMidiEvent_t;

class gthALSAMidi : public SigC::Object
{
public:
	gthALSAMidi (const char *argname)
		throw(thIOException);
	~gthALSAMidi (void);

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

#endif /* GTH_ALSAMIDI_H */
