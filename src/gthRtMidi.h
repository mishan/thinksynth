/*
 * Copyright (C) 2004-2014 Metaphonic Labs
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

#ifndef GTH_RTMIDI_H
#define GTH_RTMIDI_H 1

#include <string>
#include <vector>

#include <RtMidi.h>

#include "gthMidiQueue.h"

/* MIDI input.
 *
 * The problem this solves is not "which MIDI API" so much as "which thread".
 *
 * gthALSAMidi handed the ALSA sequencer's poll descriptors to
 * Glib::signal_io(), so MIDI arrived on the GUI thread and processmidi()
 * could call thSynth::addNote() -- which copy-constructs an entire synth tree
 * and is emphatically not something to do on a real-time thread. That was
 * correct, and it is the property worth preserving. But it is built on file
 * descriptors, which Windows has no equivalent of for MIDI, and RtMidi
 * delivers on a thread of its own regardless.
 *
 * So: the RtMidi callback pushes into an SPSC ring and pokes a
 * Glib::Dispatcher, which is precisely the "wake the GTK main loop from
 * another thread" primitive and is implemented on Win32 as well as on Unix.
 * The GUI thread drains the ring and dispatches exactly as before, so
 * everything downstream -- thMidiController, thMidiControllerConnection,
 * MidiMap, the on-screen keyboard's note signals -- is untouched.
 *
 * Pushing straight into thSynth's command queue from the RtMidi thread would
 * skip a hop, but it would lose the note-on/off signals the on-screen
 * keyboard draws from, and would make that queue multi-producer when it is
 * single-producer by construction.
 */
class gthRtMidi {
public:
    explicit gthRtMidi (const std::string &clientName,
                        const std::string &api = "",
                        const std::string &port = "");
    ~gthRtMidi (void);

    bool opened (void) const { return midi_ != NULL; }

    /* Emitted on the GUI thread, once per message. */
    sigc::signal<void(const gthMidiEvent &)> &signal_event (void) {
        return queue_.signal_event();
    }

    std::vector<std::string> ports (void) const;
    std::string portName (void) const { return portName_; }
    std::string apiName (void) const;

    /* Enumerate without opening anything.
     *
     * Constructing a gthRtMidi opens a port and installs a callback, which is
     * the wrong thing to do just to print a list -- it has side effects, it
     * prints, and on a platform without virtual ports it can fail outright
     * and report "RtMidi would not start" when RtMidi is perfectly able to
     * enumerate. RtMidiIn's constructor alone opens nothing. */
    static std::vector<std::string> probePorts (const std::string &clientName,
                                                const std::string &api = "");

    /* Messages dropped because the queue was full. Worth surfacing rather
       than silently losing notes. */
    unsigned long overflows (void) const { return queue_.overflows(); }

private:
    static void trampoline (double stamp, std::vector<unsigned char> *message,
                            void *user);

    RtMidiIn *midi_;

    gthMidiQueue queue_;

    std::string portName_;
};

#endif /* GTH_RTMIDI_H */
