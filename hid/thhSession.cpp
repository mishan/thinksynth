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

#include "thhHands.h"
#include "thhIndex.h"
#include "thhProfile.h"
#include "thhSession.h"

thhSession::thhSession (
    const std::function<void (const std::vector<thhAction> &)> &sound,
    const std::function<double ()> &now,
    const std::function<void (const std::map<std::string, std::string> &)>
        &remember)
    : sound_(sound), now_(now), remember_(remember), generation_(0),
      pair_(false)
{
    mappers_[0].shareWith(&mappers_[1]);
    mappers_[1].shareWith(&mappers_[0]);
}

thhSession::~thhSession ()
{
    release();
}

int
thhSession::add (const std::string &serial)
{
    Slot s;

    s.serial = serial;
    slots_.push_back(s);
    generation_++;
    return (int)slots_.size() - 1;
}

void
thhSession::remove (int i)
{
    slots_.erase(slots_.begin() + i);
    generation_++;
}

std::string
thhSession::handOf (int i) const
{
    return pair_ ? slots_[i].hand : "";
}

const thHidBlock &
thhSession::profileOf (int i) const
{
    return pair_ ? profiles_[1 + side(slots_[i].hand)] : profiles_[0];
}

/* A profile that is wrong still plays the controller's own, as at start. */
bool
thhSession::load (const std::string &player, const std::string &name,
                  const std::string &patch, const std::string &patchName,
                  std::string &err)
{
    const char *hand[3] = { "", "left", "right" };
    std::string again;
    bool ok = true;

    /* The channel the profile plays, without the patch's block: the block
       is the one of the patch on that channel. */
    thHidBlock base;
    std::string ignored;

    thhLoadProfileText(player, name, thhIndexControls(), THH_INDEX_PROFILE, "",
                       base, ignored);
    playChannel_ = base.channel - 1;

    patchText_ = patch;
    patchName_ = patchName;
    patchError_.clear();

    for (int i = 0; i < 3; i++)
    {
        std::string theirs;

        if (!thhLoadProfileText(player, name, thhIndexControls(),
                                THH_INDEX_PROFILE, hand[i], profiles_[i],
                                i ? again : err, patchText_,
                                patchName_ + "'s controller block", &theirs))
            ok = false;
        if (patchError_.empty())
            patchError_ = theirs;
    }
    if (ok || err.empty())
        err = again;

    configure(pair_);
    generation_++;
    return ok;
}

/* Each mapper given what its hand plays, alone or in a pair: the notes the
   old profile held end. */
void
thhSession::configure (bool pair)
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

/* Which hand each connected controller plays (thhAssignHands), and alone
   or in a pair. */
void
thhSession::hands ()
{
    std::vector<thhHandSlot> give;
    int playing = 0;

    for (size_t i = 0; i < slots_.size(); i++)
    {
        const auto it = slots_[i].serial.empty() ? remembered_.end()
                                                 : remembered_.find(slots_[i].serial);

        give.push_back({ slots_[i].latest.connected, slots_[i].said,
                         it == remembered_.end() ? "" : it->second,
                         slots_[i].hand });
    }
    thhAssignHands(give);
    for (size_t i = 0; i < slots_.size(); i++)
    {
        if (give[i].hand != slots_[i].hand)
            generation_++;
        slots_[i].hand = give[i].hand;
        playing += give[i].connected && !give[i].hand.empty();
    }

    if ((playing > 1) != pair_)
    {
        configure(playing > 1);
        generation_++;
    }
}

/* Two controllers playing, and neither saying which hand it is. */
bool
thhSession::canSwapHands () const
{
    int playing = 0;

    for (size_t i = 0; i < slots_.size(); i++)
        playing += slots_[i].latest.connected && !slots_[i].hand.empty() &&
                   slots_[i].said.empty();
    return playing == 2;
}

/* The pair's hands each other's, remembered by serial number for next
   time, and the mappers given them afresh: the notes the hands held end. */
void
thhSession::swapHands ()
{
    if (!canSwapHands())
        return;

    for (size_t i = 0; i < slots_.size(); i++)
    {
        Slot &s = slots_[i];

        /* Remembered only by a serial number there is: those without one
           would all share it. */
        if (s.latest.connected && !s.hand.empty())
        {
            s.hand = s.hand == "left" ? "right" : "left";
            if (!s.serial.empty())
                remembered_[s.serial] = s.hand;
        }
    }
    remember_(remembered_);

    configure(pair_);
    generation_++;
}

void
thhSession::update (int i, const std::vector<thhState> &edges,
                    const thhState &latest, const std::string &said,
                    int channel)
{
    Slot &s = slots_[i];

    /* A controller connecting, or saying another hand, is given its hand,
       and a pair made, before its mapper hears it; one going is let go of
       by the mapper that held its notes, before the pair is undone. A gone
       and back again in one drain, a controller swapped, is both. */
    const bool was = s.latest.connected;
    bool went = false;

    for (size_t e = 0; e < edges.size(); e++)
        went = went || !edges[e].connected;

    if ((!was || went || said != s.said) && latest.connected)
    {
        s.latest = latest;
        s.said = said;
        hands();
    }
    /* A third, which plays nothing, but whose going is heard: else it
       would be connected for ever, and take a hand the next time one is
       free. */
    if (s.hand.empty())
    {
        s.latest = latest;
        if ((was || went) && !latest.connected)
            hands();
        return;
    }

    const int h = side(s.hand);

    /* Each at its own report's time, so a loop records a tap read in one
       drain as long as it was, and not a stall of the host. */
    for (size_t e = 0; e <= edges.size(); e++)
    {
        const thhState &st = e < edges.size() ? edges[e] : latest;
        std::vector<thhAction> acts;

        mappers_[h].update(st, channel, acts);
        play(h, acts, st.time);
    }
    s.latest = latest;

    if ((was || went) && !latest.connected)
        hands();
}

void
thhSession::play (int hand, const std::vector<thhAction> &acts, double at)
{
    const double t = at > 0 ? at : now_();
    std::vector<thhAction> out;

    for (size_t i = 0; i < acts.size(); i++)
    {
        thhAction a = acts[i];
        const std::pair<int, int> key(a.channel, a.note);

        if (a.kind == thhAction::LOOP)
        {
            looper_.act(a, t, out);
            generation_++;
            continue;
        }
        if (a.kind == thhAction::NOTE_BEND)
        {
            bends_[hand][key] = a.t;
            a.t = bends_[0][key] + bends_[1][key];
        }
        else if (a.kind == thhAction::NOTE_ON || a.kind == thhAction::NOTE_OFF)
        {
            /* Both hands': the other's share of a note that ended stays
               where the share last was, and would bend the key's next. */
            bends_[0].erase(key);
            bends_[1].erase(key);
        }
        out.push_back(a);
    }

    looper_.heard(out, t);
    sounded(out);
}

void
thhSession::sounded (const std::vector<thhAction> &acts)
{
    std::vector<thhAction> heard;

    voices_.pass(acts, heard);
    sound_(heard);
}

bool
thhSession::tick ()
{
    std::vector<thhAction> acts;

    looper_.tick(now_(), acts);
    sounded(acts);
    return looper_.loops() > 0;
}

void
thhSession::release ()
{
    std::vector<thhAction> acts;

    clearLoops();
    for (int h = 0; h < 2; h++)
    {
        acts.clear();
        mappers_[h].release(acts);
        play(h, acts);
    }
}

std::string
thhSession::patch () const
{
    if (patchText_.empty())
        return "";
    return patchError_.empty()
           ? "the controller block of " + patchName_
           : "the controller block of " + patchName_ + " isn't played: " +
             patchError_;
}

std::string
thhSession::loops () const
{
    const int n = looper_.loops();
    const std::string playing = std::to_string(n) + " playing";

    return looper_.recording() ? "recording" + (n ? ", " + playing : "")
           : n ? playing : "";
}

void
thhSession::clearLoops ()
{
    std::vector<thhAction> acts;
    thhAction clear = thhAction();

    clear.kind = thhAction::LOOP;
    clear.arg = "clear";
    looper_.act(clear, now_(), acts);
    sounded(acts);
    generation_++;
}

void
thhSession::rename (const std::string &patchName)
{
    patchName_ = patchName;
    generation_++;
}
