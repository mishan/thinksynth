/* $Id: gthALSAMidi.cpp,v 1.5 2004/05/05 06:20:53 misha Exp $ */

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include <alsa/asoundlib.h>

#include <gtkmm.h>

#include "think.h"

#include "thfALSAMidi.h"

extern Glib::RefPtr<Glib::MainContext> mainContext;

thfALSAMidi::thfALSAMidi (const char *argname)
	throw (thIOException)
{
	name = argname;
	device = ALSA_DEFAULT_DEVICE;

	if(!open_seq ())
		throw errno;

}

thfALSAMidi::~thfALSAMidi (void)
{
	snd_seq_close(seq_handle);
}

bool thfALSAMidi::ProcessEvents (void)
{
	bool r = false;

	return r;
	
	/* XXX */

	if (poll(pfds, seq_nfds, 0) > 0)
	{
		int j;

		for (j = 0; j < seq_nfds; j++)
		{
			if (pfds[j].revents > 0)
			{
				m_sigMidiEvent(seq_handle);
				r = true;
			}
		}
	}

	return r;
}

sigMidiEvent_t thfALSAMidi::signal_midi_event (void)
{
	return m_sigMidiEvent;
}

bool thfALSAMidi::open_seq (void)
{
	int client_id, port_id;

    if (snd_seq_open(&seq_handle, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0)
	{
        fprintf(stderr, "Error opening ALSA sequencer.\n");
		return false;
    }

	client_id = snd_seq_set_client_name(seq_handle, name.c_str());
	
    if ((port_id = snd_seq_create_simple_port(seq_handle, name.c_str(),
						SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE,
						SND_SEQ_PORT_TYPE_APPLICATION)) < 0)
	{
        fprintf(stderr, "Error creating sequencer port.\n");
		return false;
    }

	seq_nfds = snd_seq_poll_descriptors_count(seq_handle, POLLIN);
	pfds = (struct pollfd *)malloc(sizeof(struct pollfd) * seq_nfds);
	snd_seq_poll_descriptors(seq_handle, pfds, seq_nfds, POLLIN);

	/* XXX: is this portable??? */
	mainContext->signal_io().connect(SigC::slot(*this,
												&thfALSAMidi::pollMidiEvent),
									 pfds[0].fd, Glib::IO_IN,
									 Glib::PRIORITY_DEFAULT);

	return true;
}

bool thfALSAMidi::pollMidiEvent (Glib::IOCondition cond)
{
	m_sigMidiEvent(seq_handle);

	return true;
}
