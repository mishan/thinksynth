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

#ifndef THH_LOOPER_H
#define THH_LOOPER_H

#include <map>
#include <set>
#include <utility>
#include <vector>

#include "thhMapper.h"

/* Loops of the notes the hands play, each as long as its recording took:
   a `loop record' starts one and the next ends it, and from its end it
   plays over and over, on top of the loops before it. Only notes started
   while it records are in it, with their bends and aux values, and one
   still held when it ends is let go at the end of every lap. Times are
   seconds, on a clock that only goes on. */
class thhLooper
{
public:
    /* What the hands played at now. */
    void heard (const std::vector<thhAction> &acts, double now);

    /* A LOOP action. A loop stopped lets go, in out, of what it holds. */
    void act (const thhAction &a, double now, std::vector<thhAction> &out);

    /* What the loops play from the last tick up to now. */
    void tick (double now, std::vector<thhAction> &out);

    bool recording () const { return recording_; }
    int loops () const { return (int)loops_.size(); }

private:
    struct Event
    {
        double    at;           /* into the lap */
        thhAction a;
    };

    struct Loop
    {
        int                 channel;
        double              start, length, done;
        std::vector<Event>  events;
        std::multiset<int>  sounding;
    };

    bool                         recording_ = false;
    Loop                         take_;
    std::map<std::pair<int, int>, int> open_;   /* notes the take started */
    std::vector<Loop>            loops_;

    void stop (Loop &l, std::vector<thhAction> &out);
};

/* What reaches the synth of what the hands and the loops play. The synth
   has one voice for a note on a channel, so a note sounds from the first
   start of it to the last end, and a start while it sounds, or an end
   before the last, is left out. */
class thhVoices
{
public:
    void pass (const std::vector<thhAction> &in, std::vector<thhAction> &out);

private:
    std::map<std::pair<int, int>, int> held_;
};

#endif /* THH_LOOPER_H */
