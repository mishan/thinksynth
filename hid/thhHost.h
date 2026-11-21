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

#ifndef THH_HOST_H
#define THH_HOST_H

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "thhIndex.h"
#include "thhIndexWireless.h"
#include "thhSession.h"

/* Actions as the page plays them, JSON: an array of objects whose kind is
   on, off, value, bend, aux or loop, with channel, note, velocity, arg, t
   (0..1 along the control; semitones; the aux's value), aux, and the
   profile's min and max where it gave them. Every controller the page
   plays speaks this, the Index through thhHost::take and the phone too. */
std::string thhActionsJson (const std::vector<thhAction> &actions);

/* A session fed a controller's raw reports, for a host that reads the
   devices itself and wants what to play back as text: the browser's page
   (wasm/web/thinkhid.cpp), and the native tool that holds it to the same
   answers (scripts/hidactions.cpp). Each slot is a controller, wired or on
   a dongle, by an id that stays its own as others come and go. */
class thhHost
{
public:
    /* Which of a slot's interfaces a report is from: a wired controller's
       input or IMU, or a dongle's one. */
    enum From { INPUT, IMU, RADIO };

    thhHost ();

    /* A controller connected, with the hand and serial number its
       configuration says (thhIndexConfig), either empty; its id. For a
       dongle, once its controller answers: not when the dongle opens,
       which may have none. */
    int add (const std::string &hand, const std::string &serial, double now);

    /* A report from slot id. True if it says the controller has gone, as
       a dongle does: its slot is then to be dropped, and connected again
       once one answers. */
    bool report (int id, From from, const uint8_t *data, size_t len,
                 double now);

    /* A slot's controller gone: its notes end, and its slot with it. */
    void drop (int id, double now);

    /* The keyboard's channel, 0-based, for bindings that name none. */
    void channel (int ch) { channel_ = ch; }

    /* The player's profile, and the patch's controller block with that
       patch's name and the args it has (for the pane's warnings): what is
       wrong, or empty. A patch with the same block is only named again,
       which lets go of nothing. */
    std::string profile (const std::string &text);
    std::string patch (const std::string &block, const std::string &name,
                       const std::set<std::string> &args);

    /* The hands swapped before, by serial number, and as they are now. */
    void remembered (const std::map<std::string, std::string> &hands);
    const std::map<std::string, std::string> &kept () const { return kept_; }

    /* What the controller pane shows, as JSON: the hands of a pair, each
       connected controller's bindings (thhPaneRows; a pair's under their
       hand), whether the hands can be swapped, and the loops' and the
       patch's lines. */
    std::string pane () const;

    thhSession &session () { return session_; }

    /* The loops' due notes; true while they play. */
    bool tick (double now);
    void release (double now);

    /* What to play since last asked, as thhActionsJson. */
    std::string take ();

private:
    struct Slot
    {
        int              id;
        std::string      hand;
        thhState         state;
        thhIndexImu      imu;
        thhIndexWireless wireless;
    };

    double                 now_;
    int                    channel_, next_;
    std::string            profile_, block_, name_;
    std::set<std::string>  args_;
    std::map<std::string, std::string> kept_;
    std::vector<thhAction> taken_;
    thhSession             session_;
    std::vector<Slot>      slots_;

    int at (int id) const;
};

#endif /* THH_HOST_H */
