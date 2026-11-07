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

#include <cmath>

#include "thhLooper.h"

/* Shorter than this is a slip of the hand, not a loop. */
#define THH_LOOP_SHORTEST 0.05

void
thhLooper::heard (const std::vector<thhAction> &acts, double now)
{
    if (!recording_)
        return;

    for (const thhAction &a : acts)
    {
        const std::pair<int, int> key(a.channel, a.note);

        if (a.kind == thhAction::NOTE_ON)
            open_[key]++;
        else if (a.kind != thhAction::NOTE_OFF && a.kind != thhAction::NOTE_BEND &&
                 a.kind != thhAction::NOTE_AUX)
            continue;
        if (!open_.count(key))
            continue;
        if (a.kind == thhAction::NOTE_OFF && --open_[key] == 0)
            open_.erase(key);

        Event e = { now - take_.start, a };

        e.a.channel = take_.channel;
        take_.events.push_back(e);
    }
}

void
thhLooper::act (const thhAction &a, double now, std::vector<thhAction> &out)
{
    if (a.arg == "record" && !recording_)
    {
        take_ = Loop();
        take_.channel = a.channel;
        take_.start = now;
        open_.clear();
        recording_ = true;
        return;
    }

    if (a.arg == "record")
    {
        recording_ = false;
        take_.length = now - take_.start;
        if (take_.events.empty() || take_.length < THH_LOOP_SHORTEST)
            return;

        for (const auto &key : open_)
            for (int k = 0; k < key.second; k++)
            {
                Event e = { take_.length, thhAction() };

                e.a.kind = thhAction::NOTE_OFF;
                e.a.channel = take_.channel;
                e.a.note = key.first.second;
                take_.events.push_back(e);
            }
        /* Its first lap starts as it ends, and an event at its very start
           is in that lap. */
        take_.start = now;
        take_.done = now - 1e-9;
        loops_.push_back(take_);
        return;
    }

    const bool all = a.arg == "clear";

    if (recording_)
    {
        recording_ = false;
        if (!all)
            return;
    }
    while (!loops_.empty())
    {
        stop(loops_.back(), out);
        loops_.pop_back();
        if (!all)
            break;
    }
}

void
thhLooper::tick (double now, std::vector<thhAction> &out)
{
    for (Loop &l : loops_)
    {
        /* A tick late by laps plays only the last of them. */
        const double from = std::max(l.done, now - l.length) - l.start;
        const double to = now - l.start;

        if (to <= from)
            continue;
        /* Within a nanosecond is at: laps added up miss by that much. */
        for (double lap = std::max(0.0, std::floor(from / l.length));
             lap * l.length <= to; lap++)
            for (const Event &e : l.events)
            {
                const double at = lap * l.length + e.at;

                if (at <= from + 1e-9 || at > to + 1e-9)
                    continue;
                if (e.a.kind == thhAction::NOTE_ON)
                    l.sounding.insert(e.a.note);
                else if (e.a.kind == thhAction::NOTE_OFF &&
                         l.sounding.count(e.a.note))
                    l.sounding.erase(l.sounding.find(e.a.note));
                out.push_back(e.a);
            }
        l.done = now;
    }
}

void
thhLooper::stop (Loop &l, std::vector<thhAction> &out)
{
    for (int note : l.sounding)
    {
        thhAction a = thhAction();

        a.kind = thhAction::NOTE_OFF;
        a.channel = l.channel;
        a.note = note;
        out.push_back(a);
    }
    l.sounding.clear();
}

void
thhVoices::pass (const std::vector<thhAction> &in, std::vector<thhAction> &out)
{
    for (const thhAction &a : in)
    {
        const std::pair<int, int> key(a.channel, a.note);

        if (a.kind == thhAction::NOTE_ON && held_[key]++ > 0)
            continue;
        if (a.kind == thhAction::NOTE_OFF)
        {
            const auto h = held_.find(key);

            if (h == held_.end())
                continue;
            if (--h->second > 0)
                continue;
            held_.erase(h);
        }
        out.push_back(a);
    }
}
