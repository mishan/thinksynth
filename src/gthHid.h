/*
 * Copyright (C) 2004-2026 The thinksynth authors
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

#ifndef GTH_HID_H
#define GTH_HID_H

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <glibmm/dispatcher.h>

#include "thhPane.h"
#include "thhReader.h"
#include "thhSession.h"

class thSynth;

/* Index controllers, one or a pair, wired or each on a Watchman dongle,
   played in patch mode: each read on its own thread, mapped by the
   player's profile (hid/index in the configuration directory, on the
   controllers' own), and played on the GUI thread the way the on-screen
   keyboard plays, so the roll and a piece's Kbd input hear them too.

   One controller alone plays the profile's either-hand block, whichever
   hand it is; a pair play it each with its own hand's block on top, and
   what one bends `of = both' reaches the other's notes.

   Linux only. Made after the main window and gone before it. */
class gthHid : public thhPaneSource
{
public:
    /* channel is the keyboard's, 0-based, for bindings that name none. */
    gthHid (thSynth *synth, const std::function<int ()> &channel);
    ~gthHid ();

    int controllers () const { return session_.controllers(); }
    std::string handOf (int i) const { return session_.handOf(i); }
    const thHidBlock &profileOf (int i) const { return session_.profileOf(i); }
    thhState stateOf (int i) const { return session_.stateOf(i); }
    unsigned generation () const { return session_.generation() + generation_; }
    std::string status () const;
    std::string path () const { return path_; }
    int channel () const { return channel_(); }
    bool hasArg (int channel, const std::string &arg) const;
    bool reload (std::string &err);
    bool canSwapHands () const { return session_.canSwapHands(); }
    void swapHands () { session_.swapHands(); }
    std::string loops () const { return session_.loops(); }
    bool canPair () const;
    void pair ();
    std::string pairing () const;
    void clearLoops () { session_.clearLoops(); }
    std::string patch () const { return session_.patch(); }

private:
    /* A controller: the USB device it is at and its reader; the session's
       slot of the same index plays it. */
    struct Slot
    {
        std::string                usb, serial;
        bool                       dongle = false;
        double                     pairUntil = 0;     /* on now()'s clock */
        std::atomic<bool>          pairFailed { false };  /* the reader's */
        std::unique_ptr<thhReader> reader;
    };

    thSynth              *synth_;
    std::function<int ()> channel_;
    std::string           path_;
    unsigned              generation_;
    std::string           said_;

    /* Ticked while it has loops. */
    thhSession       session_;
    sigc::connection tick_;

    /* Before the readers, so it outlives their threads, which emit it
       until they are joined. */
    Glib::Dispatcher                    dispatcher_;
    std::vector<std::unique_ptr<Slot> > slots_;
    sigc::connection                    scan_;

    bool scan ();
    thSynthTree *patchTree (int play) const;
    void sound (const std::vector<thhAction> &acts);
    void looping ();
    void drained ();
};

#endif /* GTH_HID_H */
