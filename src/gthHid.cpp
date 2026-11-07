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

#include <chrono>
#include <cstdio>
#include <filesystem>

#include "think.h"
#include "gthHid.h"
#include "gthPrefs.h"
#include "gthSignal.h"
#include "thhIndex.h"
#include "thhIndexHidraw.h"
#include "thhPlay.h"

/* How often a new controller is looked for. */
#define GTH_HID_SCAN_MS 1000

/* How often the loops play what is due: well under what an ear hears as
   late. */
#define GTH_HID_LOOP_MS 2

static double
now ()
{
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

gthHid::gthHid (thSynth *synth, const std::function<int ()> &channel)
    : synth_(synth), channel_(channel),
      path_((std::filesystem::path(gthPrefs::prefsPath()).parent_path() /
             "hid" / "index").string()),
      generation_(0), pair_(false)
{
    std::string err;
    string **hands = gthPrefs::instance()->Get("hidhands");

    for (int i = 0; hands != NULL && hands[i] != NULL; i++)
    {
        const std::string one = gthPrefs::unescape(*hands[i]);
        const size_t eq = one.find('=');

        if (eq != std::string::npos)
            remembered_[one.substr(0, eq)] = one.substr(eq + 1);
    }

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
    tick_.disconnect();
    slots_.clear();

    clearLoops();
    for (int h = 0; h < 2; h++)
    {
        acts.clear();
        mappers_[h].release(acts);
        play(h, acts);
    }
}

/* The patch on the channel the controllers play: the one their profile,
   the player's file over the controller's own, sets, else the keyboard's.
   NULL if it has none. */
thSynthTree *
gthHid::patchTree () const
{
    thMidiChan *chan = synth_->getChannel(playChannel_ >= 0 ? playChannel_
                                                            : channel_());

    return chan ? chan->modnode() : NULL;
}

/* A reader for each controller found that hasn't one; a controller pulled
   out keeps its reader, which takes it up again if it comes back where it
   was. */
bool
gthHid::scan ()
{
    /* Another patch loaded, or the channel changed, may be another
       controller block. Only another is read again: reading the file again
       lets go of every note the controllers hold. */
    const thSynthTree *tree = patchTree();
    const std::string text = tree ? tree->controller() : std::string();
    std::string err;

    if (text != patchText_ && !reload(err))
        fprintf(stderr, "controller: %s\n", err.c_str());
    else if (tree && tree->name() != patchName_)
    {
        patchName_ = tree->name();
        generation_++;
    }

    const std::vector<thhIndexNodes> found = thhFindIndexes("/sys", "/dev");

    /* A controller gone that isn't anywhere now, moved to another port or
       unplugged, has its reader stopped: one that is back where it was is
       found again below. */
    for (size_t k = slots_.size(); k-- > 0;)
    {
        bool still = slots_[k]->latest.connected;

        for (size_t i = 0; i < found.size() && !still; i++)
            still = found[i].usb == slots_[k]->usb;
        if (!still)
        {
            slots_.erase(slots_.begin() + k);
            generation_++;
        }
    }

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
        slot->serial = found[i].serial;
        slot->reader.reset(new thhReader(
            [usb](thhConnection &c, std::string &status) {
                return thhConnectIndex("/sys", "/dev", usb, c, status);
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

/* Nothing while one is connected; else what a reader says other than that
   there is none (no permission, say), which is what can be done
   something about. */
std::string
gthHid::status () const
{
    std::string none = "no Valve Index controller is plugged in";

    for (size_t i = 0; i < slots_.size(); i++)
        if (slots_[i]->latest.connected)
            return "";
    for (size_t i = 0; i < slots_.size(); i++)
    {
        const std::string why = slots_[i]->reader->status();

        if (!why.empty() && why.compare(0, none.size(), none) != 0)
            return why;
    }
    return none;
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

    /* The channel the profile plays, without the patch's block: the block
       is the one of the patch on that channel. */
    thcHidBlock base;
    std::string ignored;

    thhLoadProfile(path_, thhIndexControls(), THH_INDEX_PROFILE, "", base,
                   ignored);
    playChannel_ = base.channel - 1;

    const thSynthTree *tree = patchTree();

    patchText_ = tree ? tree->controller() : std::string();
    patchName_ = tree ? tree->name() : std::string();
    patchError_.clear();

    for (int i = 0; i < 3; i++)
    {
        std::string theirs;

        if (!thhLoadProfile(path_, thhIndexControls(), THH_INDEX_PROFILE,
                            hand[i], profiles_[i], i ? again : err, patchText_,
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

/* Which hand each connected controller plays (thhAssignHands), and alone
   or in a pair. */
void
gthHid::hands ()
{
    std::vector<thhHandSlot> give;
    int playing = 0;

    for (size_t i = 0; i < slots_.size(); i++)
    {
        const auto it = slots_[i]->serial.empty() ? remembered_.end()
                                                  : remembered_.find(slots_[i]->serial);

        give.push_back({ slots_[i]->latest.connected, slots_[i]->reader->hand(),
                         it == remembered_.end() ? "" : it->second,
                         slots_[i]->hand });
    }
    thhAssignHands(give);
    for (size_t i = 0; i < slots_.size(); i++)
    {
        if (give[i].hand != slots_[i]->hand)
            generation_++;
        slots_[i]->hand = give[i].hand;
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
gthHid::canSwapHands () const
{
    int playing = 0;

    for (size_t i = 0; i < slots_.size(); i++)
        playing += slots_[i]->latest.connected && !slots_[i]->hand.empty() &&
                   slots_[i]->reader->hand().empty();
    return playing == 2;
}

/* The pair's hands each other's, remembered by serial number for next
   time, and the mappers given them afresh: the notes the hands held end. */
void
gthHid::swapHands ()
{
    if (!canSwapHands())
        return;

    for (size_t i = 0; i < slots_.size(); i++)
    {
        Slot &s = *slots_[i];

        /* Remembered only by a serial number there is: those without one
           would all share it. */
        if (s.latest.connected && !s.hand.empty())
        {
            s.hand = s.hand == "left" ? "right" : "left";
            if (!s.serial.empty())
                remembered_[s.serial] = s.hand;
        }
    }

    string **vals = new string *[remembered_.size() + 1];
    size_t n = 0;

    for (const auto &r : remembered_)
        vals[n++] = new string(gthPrefs::escape(r.first + "=" + r.second));
    vals[n] = NULL;
    gthPrefs::instance()->Set("hidhands", vals);

    configure(pair_);
    generation_++;
}

/* A hand's actions on the synth: notes as the keyboard plays them, each
   note's bend the sum of the hands', in the order they came, so a note's
   first bend follows it on; and the looper's, which hears what they play. */
void
gthHid::play (int hand, const std::vector<thhAction> &acts, double at)
{
    const double t = at > 0 ? at : now();
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
    sound(out);
    if (looper_.loops() && !tick_.connected())
        tick_ = Glib::signal_timeout().connect(
            sigc::mem_fun(*this, &gthHid::tick), GTH_HID_LOOP_MS);
}

void
gthHid::sound (const std::vector<thhAction> &acts)
{
    std::vector<thhAction> heard;

    voices_.pass(acts, heard);
    thhPlay(*synth_, heard, [](const thhAction &a) {
        if (a.kind == thhAction::NOTE_ON)
            m_sigKbdNoteOn(a.channel, a.note, a.vel);
        else
            m_sigKbdNoteOff(a.channel, a.note);
    });
}

bool
gthHid::tick ()
{
    std::vector<thhAction> acts;

    looper_.tick(now(), acts);
    sound(acts);
    return looper_.loops() > 0;
}

std::string
gthHid::patch () const
{
    if (patchText_.empty())
        return "";
    return patchError_.empty()
           ? "the controller block of " + patchName_
           : "the controller block of " + patchName_ + " isn't played: " +
             patchError_;
}

std::string
gthHid::loops () const
{
    const int n = looper_.loops();
    const std::string playing = std::to_string(n) + " playing";

    return looper_.recording() ? "recording" + (n ? ", " + playing : "")
           : n ? playing : "";
}

void
gthHid::clearLoops ()
{
    std::vector<thhAction> acts;
    thhAction clear = thhAction();

    clear.kind = thhAction::LOOP;
    clear.arg = "clear";
    looper_.act(clear, now(), acts);
    sound(acts);
    generation_++;
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

        /* A controller connecting, or saying another hand, is given its
           hand, and a pair made, before its mapper hears it; one going is
           let go of by the mapper that held its notes, before the pair is
           undone. A gone and back again in one drain, a controller
           swapped, is both. */
        const bool was = s.latest.connected;
        bool went = false;

        for (size_t e = 0; e < edges.size(); e++)
            went = went || !edges[e].connected;

        if ((!was || went || s.reader->hand() != s.said) && latest.connected)
        {
            s.latest = latest;
            s.said = s.reader->hand();
            hands();
        }
        /* A third, which plays nothing, but whose going is heard: else it
           would be connected for ever, and take a hand the next time one
           is free. */
        if (s.hand.empty())
        {
            s.latest = latest;
            if ((was || went) && !latest.connected)
                hands();
            continue;
        }

        const int h = side(s.hand);

        /* Each at its own report's time, so a loop records a tap read in
           one drain as long as it was, and not a stall of this thread. */
        for (size_t e = 0; e <= edges.size(); e++)
        {
            const thhState &st = e < edges.size() ? edges[e] : latest;
            std::vector<thhAction> acts;

            mappers_[h].update(st, ch, acts);
            play(h, acts, st.time);
        }
        s.latest = latest;

        if ((was || went) && !latest.connected)
            hands();
    }

    /* Said once each time it changes, not once a second. */
    const std::string status = this->status();

    if (status != said_ && !status.empty() &&
        status.find("no Valve Index") != 0)
        fprintf(stderr, "controller: %s\n", status.c_str());
    said_ = status;
}
