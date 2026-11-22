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

/* The hand the player has said each controller is, by its serial number,
   as the preferences keep it: `hidhands SERIAL=left ...'. */
static void
remember (const std::map<std::string, std::string> &hands)
{
    string **vals = new string *[hands.size() + 1];
    size_t n = 0;

    for (const auto &r : hands)
        vals[n++] = new string(gthPrefs::escape(r.first + "=" + r.second));
    vals[n] = NULL;
    gthPrefs::instance()->Set("hidhands", vals);
}

gthHid::gthHid (thSynth *synth, const std::function<int ()> &channel)
    : synth_(synth), channel_(channel),
      path_((std::filesystem::path(gthPrefs::prefsPath()).parent_path() /
             "hid" / "index").string()),
      generation_(0),
      session_([this](const std::vector<thhAction> &acts) { sound(acts); },
               now, remember)
{
    std::string err;
    string **hands = gthPrefs::instance()->Get("hidhands");
    std::map<std::string, std::string> remembered;

    for (int i = 0; hands != NULL && hands[i] != NULL; i++)
    {
        const std::string one = gthPrefs::unescape(*hands[i]);
        const size_t eq = one.find('=');

        if (eq != std::string::npos)
            remembered[one.substr(0, eq)] = one.substr(eq + 1);
    }
    session_.remembered(remembered);

    if (!reload(err))
        fprintf(stderr, "%s; playing the controller's own profile\n",
                err.c_str());

    dispatcher_.connect(sigc::mem_fun(*this, &gthHid::drained));
    scan();
    scan_ = Glib::signal_timeout().connect(sigc::mem_fun(*this, &gthHid::scan),
                                           GTH_HID_SCAN_MS);
}

gthHid::~gthHid ()
{
    scan_.disconnect();
    tick_.disconnect();
    slots_.clear();
    session_.release();
}

/* The patch on the channel the controllers play: play, the one their
   profile sets, 0-based, else (-1) the keyboard's. NULL if it has none. */
thSynthTree *
gthHid::patchTree (int play) const
{
    thMidiChan *chan = synth_->getChannel(play >= 0 ? play : channel_());

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
    const thSynthTree *tree = patchTree(session_.playChannel());
    const std::string text = tree ? tree->controller("index") : std::string();
    std::string err;

    if (text != session_.patchText() && !reload(err))
        fprintf(stderr, "controller: %s\n", err.c_str());
    else if (tree && tree->name() != session_.patchName())
        session_.rename(tree->name());

    std::vector<thhIndexNodes> found = thhFindIndexes("/sys", "/dev");
    const size_t wired = found.size();
    const std::vector<thhIndexNodes> dongles = thhFindDongles("/sys", "/dev");

    found.insert(found.end(), dongles.begin(), dongles.end());

    /* A controller gone that isn't anywhere now, moved to another port or
       unplugged, has its reader stopped: one that is back where it was is
       found again below. */
    for (size_t k = slots_.size(); k-- > 0;)
    {
        bool still = session_.stateOf(k).connected;

        for (size_t i = 0; i < found.size() && !still; i++)
            still = found[i].usb == slots_[k]->usb;
        if (!still)
        {
            slots_.erase(slots_.begin() + k);
            session_.remove(k);
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
        const auto connect = i < wired ? thhConnectIndex : thhConnectDongle;

        slot->usb = usb;
        slot->serial = found[i].serial;
        slot->dongle = i >= wired;
        slot->reader.reset(new thhReader(
            [usb, connect](thhConnection &c, std::string &status) {
                return connect("/sys", "/dev", usb, c, status);
            },
            [this]() { dispatcher_.emit(); }, 3000,
            [usb, slot]() {
                std::string why;

                if (!thhPairDongle("/sys", "/dev", usb, why))
                {
                    fprintf(stderr, "controller: %s\n", why.c_str());
                    slot->pairFailed = true;
                }
            }));
        slots_.push_back(std::unique_ptr<Slot>(slot));
        session_.add(found[i].serial);
    }
    return true;
}

/* Nothing while one is connected; else what a reader says other than that
   there is none (no permission, say), which is what can be done
   something about. */
std::string
gthHid::status () const
{
    std::string none = "no Valve Index controller is plugged in";

    for (int i = 0; i < session_.controllers(); i++)
        if (session_.stateOf(i).connected)
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

std::vector<std::string>
gthHid::knobs () const
{
    const int play = session_.playChannel();
    std::vector<std::string> out;

    /* One number each, as a binding moves it (setChanArgFraction), as the
       page's are (tw_controller). */
    for (const auto &a : synth_->getChanArgs(play >= 0 ? play : channel_()))
        if (a.second && a.second->len() == 1)
            out.push_back(a.first);
    return out;
}

/* A profile that is wrong still plays the controller's own, as at start. */
bool
gthHid::reload (std::string &err)
{
    std::string text, unread;
    const bool read = thhReadProfile(path_, text, unread);

    /* The channel the profile plays, without the patch's block: the block
       is the one of the patch on that channel. */
    thHidBlock base;
    std::string ignored;

    thhLoadProfileText(text, path_, thhIndexControls(), THH_INDEX_PROFILE, "",
                       base, ignored);

    const thSynthTree *tree = patchTree(base.channel - 1);
    const bool ok = session_.load(text, path_,
                                  tree ? tree->controller("index") : "",
                                  tree ? tree->name() : "", err);

    if (read)
        return ok;
    err = unread + (err.empty() ? "" : "; " + err);
    return false;
}

/* A dongle that answers and hears no controller, which is what pairing is
   for: not one that can't be opened, where pairing would fail too. Its own
   controller may only be turned off; pairing replaces it only if another
   pairs in its place. */
static bool
empty (const std::string &status)
{
    return status.rfind(THH_DONGLE_EMPTY, 0) == 0;
}

bool
gthHid::canPair () const
{
    for (size_t i = 0; i < slots_.size(); i++)
        if (slots_[i]->dongle && !session_.stateOf(i).connected &&
            empty(slots_[i]->reader->status()))
            return true;
    return false;
}

/* The first such dongle, for as long as it listens. */
void
gthHid::pair ()
{
    for (size_t i = 0; i < slots_.size(); i++)
        if (slots_[i]->dongle && !session_.stateOf(i).connected &&
            empty(slots_[i]->reader->status()))
        {
            slots_[i]->pairFailed = false;
            slots_[i]->reader->pair();
            slots_[i]->pairUntil = now() + 30;
            generation_++;
            return;
        }
}

std::string
gthHid::pairing () const
{
    for (size_t i = 0; i < slots_.size(); i++)
        if (!session_.stateOf(i).connected && !slots_[i]->pairFailed &&
            now() < slots_[i]->pairUntil)
            return "the dongle " + slots_[i]->serial + " is listening: turn "
                   "the controller off, then hold its System and B buttons "
                   "until its light blinks blue";
    return "";
}

void
gthHid::looping ()
{
    if (session_.looping() && !tick_.connected())
        tick_ = Glib::signal_timeout().connect(
            [this]() { return session_.tick(); }, GTH_HID_LOOP_MS);
}

void
gthHid::sound (const std::vector<thhAction> &acts)
{
    thhPlay(*synth_, acts, [](const thhAction &a) {
        if (a.kind == thhAction::NOTE_ON)
            m_sigKbdNoteOn(a.channel, a.note, a.vel);
        else
            m_sigKbdNoteOff(a.channel, a.note);
    });
}

/* Each slot's states to the session, at the keyboard's channel; and the
   loops ticked while there are any. */
void
gthHid::drained ()
{
    const int ch = channel_();

    for (size_t i = 0; i < slots_.size(); i++)
    {
        std::vector<thhState> edges;
        thhState latest;

        if (slots_[i]->reader->drain(edges, latest))
            session_.update((int)i, edges, latest, slots_[i]->reader->hand(),
                            ch);
    }
    looping();

    /* Said once each time it changes, not once a second. */
    const std::string status = this->status();

    if (status != said_ && !status.empty() &&
        status.find("no Valve Index") != 0)
        fprintf(stderr, "controller: %s\n", status.c_str());
    said_ = status;
}
