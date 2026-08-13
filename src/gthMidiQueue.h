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

#ifndef GTH_MIDIQUEUE_H
#define GTH_MIDIQUEUE_H 1

#include <atomic>

#include <glibmm/dispatcher.h>
#include <sigc++/sigc++.h>

#include "think.h"

/* One MIDI message, flattened.
 *
 * Fixed size on purpose: it travels through thRing, a lock-free SPSC queue of
 * fixed-size items. Channel voice messages are at most three bytes, which is
 * everything the synth acts on. SysEx, timing clock and active sensing are
 * filtered out at the source rather than carried across a thread boundary and
 * discarded here.
 */
struct gthMidiEvent {
    unsigned char status;
    unsigned char data1;
    unsigned char data2;
    unsigned char len;
};

/* Getting MIDI from whatever thread it arrives on to the GUI thread.
 *
 * Separate from gthRtMidi so it can be tested without a MIDI device -- see
 * scripts/dspmidi. The threading is the part of this change with something to
 * get wrong; which library the bytes came from is not.
 *
 * gthALSAMidi handed the ALSA sequencer's poll descriptors to
 * Glib::signal_io(), so MIDI arrived on the GUI thread and could call
 * thSynth::addNote(), which copy-constructs an entire synth tree. That is the
 * property worth preserving, and it is why this hands off to the GUI thread
 * rather than pushing into thSynth's command queue directly. But it is built
 * on file descriptors, which Windows has no equivalent of for MIDI, and
 * RtMidi delivers on a thread of its own regardless.
 *
 * Glib::Dispatcher is the portable replacement: it is specifically the "wake
 * the GTK main loop from another thread" primitive and is implemented on
 * Win32 as well as on Unix.
 */
class gthMidiQueue : public sigc::trackable {
public:
    /* 1024 messages is about a second of dense controller traffic. */
    typedef thRing<gthMidiEvent, 1024> Queue;

    gthMidiQueue (void);

    /* Any thread. Lock-free, allocation-free. Returns false if the queue was
       full, in which case the message is dropped rather than the producer
       blocked -- a stalled MIDI thread is worse than a lost controller
       update. */
    bool push (const gthMidiEvent &ev);

    /* Emitted on the thread that constructed this object, once per message. */
    sigc::signal<void(const gthMidiEvent &)> &signal_event (void) {
        return sigEvent_;
    }

    unsigned long overflows (void) const {
        return overflows_.load(std::memory_order_relaxed);
    }

    /* A test seam, and the only reason it exists is that the ordering in
       drain() cannot be tested any other way.
     *
     * The hook is called at the end of drain(), after the last failed pop --
     * which is exactly the window that clearing notified_ *after* the pop loop
     * would leave open. scripts/dspmidi pushes from another thread from inside
     * the hook and then requires the message to arrive. Racing for that window
     * does not work: it is about two instructions wide, and dspmidi's flood
     * phase failed to hit it in 60,000 messages against a deliberately
     * inverted build. Standing in it deliberately does work.
     *
     * NULL everywhere but the harness, and one predictable branch per drain. */
    typedef void (*DrainHook) (void *user);

    void setDrainHook (DrainHook hook, void *user) {
        drainHook_ = hook;
        drainHookUser_ = user;
    }

private:
    void drain (void);

    DrainHook drainHook_;
    void *drainHookUser_;

    Queue queue_;
    Glib::Dispatcher dispatcher_;

    /* Coalesces a burst into one main-loop wake-up: the producer only pokes
       the dispatcher when the consumer is not already on its way.
       Glib::Dispatcher::emit() writes to a pipe, and a pipe can fill. */
    std::atomic<bool> notified_;
    std::atomic<unsigned long> overflows_;

    sigc::signal<void(const gthMidiEvent &)> sigEvent_;
};

#endif /* GTH_MIDIQUEUE_H */
