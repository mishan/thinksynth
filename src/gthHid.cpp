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

#include <cstdio>
#include <filesystem>

#include "think.h"
#include "gthHid.h"
#include "gthPrefs.h"
#include "gthSignal.h"
#include "thhIndex.h"
#include "thhPlay.h"

/* How often a new controller is looked for. */
#define GTH_HID_SCAN_MS 1000

gthHid::gthHid (thSynth *synth, const std::function<int ()> &channel)
    : synth_(synth), channel_(channel),
      path_((std::filesystem::path(gthPrefs::prefsPath()).parent_path() /
             "hid" / "index").string()),
      generation_(0), pair_(false)
{
    std::string err;

    if (!reload(err))
        fprintf(stderr, "%s; playing the controller's own profile\n",
                err.c_str());

    mappers_[0].shareWith(&mappers_[1]);
    mappers_[1].shareWith(&mappers_[0]);
    dispatcher_.connect(sigc::mem_fun(*this, &gthHid::drained));
    scan();
    scan_ = Glib::signal_timeout().connect(sigc::mem_fun(*this, &gthHid::scan),
                                           GTH_HID_SCAN_MS);
}

gthHid::~gthHid ()
{
    std::vector<thhAction> acts;

    scan_.disconnect();
    slots_.clear();
    for (int h = 0; h < 2; h++)
    {
        acts.clear();
        mappers_[h].release(acts);
        play(h, acts);
    }
}

/* A reader for each controller found that hasn't one; a controller pulled
   out keeps its reader, which takes it up again if it comes back where it
   was. */
bool
gthHid::scan ()
{
    const std::vector<thhIndexNodes> found = thhFindIndexes("/sys", "/dev");

    for (size_t i = 0; i < found.size(); i++)
    {
        bool have = false;

        for (size_t k = 0; k < slots_.size(); k++)
            have = have || slots_[k]->usb == found[i].usb;
        if (have)
            continue;

        Slot *slot = new Slot;
        const std::string usb = found[i].usb;

        slot->usb = usb;
        slot->reader.reset(new thhReader(
            [usb](int &input, int &imu, std::vector<int> &fds, std::string &hand,
                  std::string &status) {
                return thhConnectIndex("/sys", "/dev", usb, input, imu, fds,
                                       hand, status);
            },
            [this]() { dispatcher_.emit(); }));
        slots_.push_back(std::unique_ptr<Slot>(slot));
        generation_++;
    }
    return true;
}

std::string
gthHid::handOf (int i) const
{
    return pair_ ? slots_[i]->hand : "";
}

const thcHidBlock &
gthHid::profileOf (int i) const
{
    return pair_ ? profiles_[1 + side(slots_[i]->hand)] : profiles_[0];
}

std::string
gthHid::status () const
{
    for (size_t i = 0; i < slots_.size(); i++)
        if (!slots_[i]->reader->status().empty())
            return slots_[i]->reader->status();
    return slots_.empty() ? "no Valve Index controller is plugged in" : "";
}

bool
gthHid::hasArg (int channel, const std::string &arg) const
{
    return synth_->getChanArg(channel, arg) != NULL;
}

/* A profile that is wrong still plays the controller's own, as at start. */
bool
gthHid::reload (std::string &err)
{
    const char *hand[3] = { "", "left", "right" };
    std::string again;
    bool ok = true;

    for (int i = 0; i < 3; i++)
        if (!thhLoadProfile(path_, thhIndexControls(), THH_INDEX_PROFILE,
                            hand[i], profiles_[i], i ? again : err))
            ok = false;
    if (ok || err.empty())
        err = again;

    configure(pair_);
    generation_++;
    return ok;
}

/* Each mapper given what its hand plays, alone or in a pair: the notes the
   old profiles held end. */
void
gthHid::configure (bool pair)
{
    std::string ignored;
    int line;

    pair_ = pair;
    for (int h = 0; h < 2; h++)
    {
        std::vector<thhAction> offs;

        mappers_[h].configure(profiles_[pair ? 1 + h : 0], thhIndexControls(),
                              offs, ignored, line);
        play(h, offs);
    }
}

/* Which hand each connected controller plays: the one it says, or, one
   that doesn't, whichever is free; and alone or in a pair. */
void
gthHid::hands ()
{
    int connected = 0;

    for (size_t i = 0; i < slots_.size(); i++)
    {
        Slot &s = *slots_[i];

        if (!s.latest.connected)
            continue;
        connected++;

        const std::string said = s.reader->hand();

        if (!said.empty() && said != s.hand)
        {
            s.hand = said;
            generation_++;
        }
        if (s.hand.empty())
        {
            bool right = false;

            for (size_t k = 0; k < slots_.size(); k++)
                right = right || (k != i && slots_[k]->hand == "right");
            s.hand = right ? "left" : "right";
            generation_++;
        }
    }

    if ((connected > 1) != pair_)
    {
        configure(connected > 1);
        generation_++;
    }
}

/* A hand's actions on the synth: notes as the keyboard plays them, each
   note's bend the sum of the hands', the rest as they are. */
void
gthHid::play (int hand, const std::vector<thhAction> &acts)
{
    std::vector<thhAction> rest;

    for (size_t i = 0; i < acts.size(); i++)
    {
        const thhAction &a = acts[i];
        const std::pair<int, int> key(a.channel, a.note);

        if (a.kind == thhAction::NOTE_BEND)
        {
            bends_[hand][key] = a.t;
            synth_->bendNote(a.channel, a.note,
                             (float)(bends_[0][key] + bends_[1][key]));
            continue;
        }
        if (a.kind == thhAction::NOTE_ON || a.kind == thhAction::NOTE_OFF)
        {
            bends_[0].erase(key);
            bends_[1].erase(key);
        }
        rest.push_back(a);
    }

    thhPlay(*synth_, rest, [](const thhAction &a) {
        if (a.kind == thhAction::NOTE_ON)
            m_sigKbdNoteOn(a.channel, a.note, a.vel);
        else
            m_sigKbdNoteOff(a.channel, a.note);
    });
}

void
gthHid::drained ()
{
    const int ch = channel_();

    for (size_t i = 0; i < slots_.size(); i++)
    {
        Slot &s = *slots_[i];
        std::vector<thhState> edges;
        thhState latest;

        if (!s.reader->drain(edges, latest))
            continue;

        /* A controller connecting is given its hand, and a pair made,
           before its mapper hears it; one going is let go of by the mapper
           that held its notes, before the pair is undone. */
        const bool was = s.latest.connected;

        if (!was && latest.connected)
        {
            s.latest = latest;
            hands();
        }

        const int h = pair_ ? side(s.hand) : 0;
        std::vector<thhAction> acts;

        for (size_t e = 0; e < edges.size(); e++)
            mappers_[h].update(edges[e], ch, acts);
        mappers_[h].update(latest, ch, acts);
        s.latest = latest;
        play(h, acts);

        if (was && !latest.connected)
            hands();
    }

    /* Said once each time it changes, not once a second. */
    const std::string status = this->status();

    if (status != said_ && !status.empty() &&
        status.find("no Valve Index") != 0)
        fprintf(stderr, "controller: %s\n", status.c_str());
    said_ = status;
}
