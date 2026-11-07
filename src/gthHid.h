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

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <glibmm/dispatcher.h>

#include "thhHands.h"
#include "thhMapper.h"
#include "thhPane.h"
#include "thhReader.h"

class thSynth;

/* Wired Index controllers, one or a pair, played in patch mode: each read
   on its own thread, mapped by the player's profile (hid/index in the
   configuration directory, on the controllers' own), and played on the GUI
   thread the way the on-screen keyboard plays, so the roll and a piece's
   Kbd input hear them too.

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

    int controllers () const { return (int)slots_.size(); }
    std::string handOf (int i) const;
    const thcHidBlock &profileOf (int i) const;
    thhState stateOf (int i) const { return slots_[i]->latest; }
    unsigned generation () const { return generation_; }
    std::string status () const;
    std::string path () const { return path_; }
    int channel () const { return channel_(); }
    bool hasArg (int channel, const std::string &arg) const;
    bool reload (std::string &err);
    bool canSwapHands () const;
    void swapHands ();

private:
    /* A controller: the USB device it is at, its reader, and the hand it
       plays as once it has said or been given one. */
    struct Slot
    {
        std::string                usb, serial, hand, said;
        std::unique_ptr<thhReader> reader;
        thhState                   latest;
    };

    thSynth              *synth_;
    std::function<int ()> channel_;
    std::string           path_;
    unsigned              generation_;
    std::string           said_;
    bool                  pair_;

    /* What either hand plays alone, and each in a pair; and a mapper for
       each hand. */
    thcHidBlock profiles_[3];
    thhMapper   mappers_[2];

    /* The hand the player has said each controller is, by its serial
       number, as the preferences keep it: `hidhands SERIAL=left ...'. */
    std::map<std::string, std::string> remembered_;

    /* Each hand's bend of each note, which add. */
    std::map<std::pair<int, int>, double> bends_[2];

    /* Before the readers, so it outlives their threads, which emit it
       until they are joined. */
    Glib::Dispatcher                    dispatcher_;
    std::vector<std::unique_ptr<Slot> > slots_;
    sigc::connection                    scan_;

    static int side (const std::string &hand) { return hand == "left" ? 0 : 1; }
    bool scan ();
    void hands ();
    void configure (bool pair);
    void play (int hand, const std::vector<thhAction> &acts);
    void drained ();
};

#endif /* GTH_HID_H */
