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

gthHid::gthHid (thSynth *synth, const std::function<int ()> &channel)
    : synth_(synth), channel_(channel),
      path_((std::filesystem::path(gthPrefs::prefsPath()).parent_path() /
             "hid" / "index").string()),
      generation_(0)
{
    std::string err;

    if (!reload(err))
        fprintf(stderr, "%s; playing the controller's own profile\n",
                err.c_str());

    dispatcher_.connect(sigc::mem_fun(*this, &gthHid::drained));
    reader_.reset(new thhReader(
        [](int &input, int &imu, std::vector<int> &fds, std::string &status) {
            return thhConnectIndex("/sys", "/dev", input, imu, fds, status);
        },
        [this]() { dispatcher_.emit(); }));
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
    std::vector<thhAction> offs;
    std::string ignored;
    int line;
    const bool ok = thhLoadProfile(path_, thhIndexControls(), THH_INDEX_PROFILE,
                                   profile_, err);

    mapper_.configure(profile_, thhIndexControls(), offs, ignored, line);
    thhPlay(*synth_, offs, NULL);
    generation_++;
    return ok;
}

gthHid::~gthHid ()
{
    reader_.reset();

    std::vector<thhAction> acts;

    mapper_.release(acts);
    thhPlay(*synth_, acts, NULL);
}

void
gthHid::drained ()
{
    std::vector<thhState> edges;
    std::vector<thhAction> acts;
    thhState latest;
    const int ch = channel_();

    if (reader_->drain(edges, latest))
    {
        for (size_t i = 0; i < edges.size(); i++)
            mapper_.update(edges[i], ch, acts);
        mapper_.update(latest, ch, acts);
        latest_ = latest;
    }

    thhPlay(*synth_, acts, [](const thhAction &a) {
        if (a.kind == thhAction::NOTE_ON)
            m_sigKbdNoteOn(a.channel, a.note, a.vel);
        else
            m_sigKbdNoteOff(a.channel, a.note);
    });

    /* Said once each time it changes, not once a second. */
    const std::string status = reader_->status();

    if (status != said_ && !status.empty() &&
        status.find("no Valve Index") != 0)
        fprintf(stderr, "controller: %s\n", status.c_str());
    said_ = status;
}
