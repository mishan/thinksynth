/* $Id: gthALSAMidi.cpp,v 1.14 2004/09/19 04:44:25 joshk Exp $ */
/*
 * Copyright (C) 2004 Metaphonic Labs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include <alsa/asoundlib.h>

#include <gtkmm.h>

#include "think.h"

#include "gthALSAMidi.h"

gthALSAMidi::gthALSAMidi (const char *argname)
	throw (thIOException)
{
	name = argname;
	device = ALSA_DEFAULT_MIDI_DEVICE;
	_seq_opened = open_seq();
}

gthALSAMidi::~gthALSAMidi (void)
{
	snd_seq_close(seq_handle);
}

bool gthALSAMidi::ProcessEvents (void)
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

sigMidiEvent_t gthALSAMidi::signal_midi_event (void)
{
	return m_sigMidiEvent;
}

bool gthALSAMidi::open_seq (void)
{
	int port_id;

    if (snd_seq_open(&seq_handle, device.c_str(), SND_SEQ_OPEN_DUPLEX, 0) < 0)
	{
        fprintf(stderr, "Error opening ALSA sequencer.\n");
		return false;
    }

	snd_seq_set_client_name(seq_handle, name.c_str());
	
    if ((port_id = snd_seq_create_simple_port(seq_handle, name.c_str(),
						SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE,
						SND_SEQ_PORT_TYPE_APPLICATION)) < 0)
	{
        fprintf(stderr, "Error creating sequencer port.\n");
		return false;
    }
	else
	{
		printf("Opened sequencer port for ALSA on %d:%d\n",
			snd_seq_client_id(seq_handle), port_id);
	}

	seq_nfds = snd_seq_poll_descriptors_count(seq_handle, POLLIN);
	pfds = (struct pollfd *)malloc(sizeof(struct pollfd) * seq_nfds);
	snd_seq_poll_descriptors(seq_handle, pfds, seq_nfds, POLLIN);

	/* XXX: is this portable??? */
	Glib::signal_io().connect(SigC::slot(*this,
										 &gthALSAMidi::pollMidiEvent),
							  pfds[0].fd, Glib::IO_IN, Glib::PRIORITY_HIGH);
	/* XXX */
//	snd_seq_set_input_buffer_size(seq_handle, 50000);

//	printf("current MIDI size: %d\n",
//		   snd_seq_get_input_buffer_size(seq_handle));

	
	return true;
}

bool gthALSAMidi::pollMidiEvent (Glib::IOCondition cond)
{
	m_sigMidiEvent(seq_handle);

	return true;
}
