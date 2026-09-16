/*
 * Copyright (C) 2004-2026 Metaphonic Labs
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

/*
 * The tape, laid out for JavaScript to read straight out of the heap.
 *
 * Both Emscripten hosts deliver one: thinkwasm.cpp under Node, for
 * genwav.mjs, and thinkweb.cpp in the worklet, for the page. M2's gate is
 * that the two tapes are the same tape (JAM.md, section 6), which they can
 * only be if they are spelled the same way -- so the spelling is here,
 * once, rather than twice. wasm/tape.mjs is the other half: the offsets
 * below, read from JavaScript, and the line each event prints as.
 */

#ifndef TH_WASM_EVENT_H
#define TH_WASM_EVENT_H 1

#include <stdint.h>
#include <stddef.h>

#include <deque>
#include <string>
#include <vector>

#include "libthink/thcomposer.h"

/* `kind' is the tape's letter rather than the enum, so the enum's numbering
   stays this side of the boundary -- except for an event the tape has no
   letter for, whose raw type rides in `note'. */
struct twEvent
{
    double      at;
    double      duration;   /* N */
    double      value;      /* C, E */
    int32_t     kind;       /* 'N', 'C', 'P', 'E' or '?' */
    int32_t     channel;
    int32_t     note;       /* N; the THC_EV_* value for '?' */
    int32_t     velocity;   /* N */
    const char *name;       /* C: chanarg, P: patch, E: node */
    const char *arg;        /* E */
};

static_assert(offsetof(twEvent, at) == 0, "tape.mjs reads at 0");
static_assert(offsetof(twEvent, duration) == 8, "tape.mjs reads 8");
static_assert(offsetof(twEvent, value) == 16, "tape.mjs reads 16");
static_assert(offsetof(twEvent, kind) == 24, "tape.mjs reads 24");
static_assert(offsetof(twEvent, channel) == 28, "tape.mjs reads 28");
static_assert(offsetof(twEvent, note) == 32, "tape.mjs reads 32");
static_assert(offsetof(twEvent, velocity) == 36, "tape.mjs reads 36");
static_assert(offsetof(twEvent, name) == 40, "tape.mjs reads 40");
static_assert(offsetof(twEvent, arg) == 44, "tape.mjs reads 44");
static_assert(sizeof(twEvent) == 48, "tape.mjs steps by 48");

/* What the scheduler delivered since the last clear.
 *
 * Connect deliver() to thcScheduler::sigDelivered; the events are then a
 * flat array the other side reads without a call per field. */
class twTape
{
public:
    void deliver (const thcEvent &ev)
    {
        twEvent e = {};

        e.at = ev.at;
        e.channel = ev.channel;

        switch (ev.type)
        {
            case THC_EV_NOTE:
                e.kind = 'N';
                e.note = ev.u.note.note;
                e.velocity = ev.u.note.velocity;
                e.duration = ev.u.note.duration;
                break;
            case THC_EV_CHANARG:
                e.kind = 'C';
                e.name = keep(ev.u.chanarg.name);
                e.value = ev.u.chanarg.value;
                break;
            case THC_EV_PATCH:
                e.kind = 'P';
                e.name = keep(ev.u.patch.name);
                break;
            case THC_EV_NODEARG:
                e.kind = 'E';
                e.name = keep(ev.u.nodearg.node);
                e.arg = keep(ev.u.nodearg.arg);
                e.value = ev.u.nodearg.value;
                break;
            default:
                e.kind = '?';
                e.note = (int)ev.type;
                break;
        }

        events_.push_back(e);
    }

    size_t         count (void) const { return events_.size(); }
    const twEvent *data (void) const { return events_.data(); }

    void clear (void)
    {
        events_.clear();
        strings_.clear();
    }

private:
    /* The event's strings are the scheduler's and need not outlive
       delivery; these copies live until the next clear(). A deque, because
       growing one leaves the earlier elements -- and so their c_str()s --
       where they were. */
    const char *keep (const char *s)
    {
        strings_.emplace_back(s != NULL ? s : "");

        return strings_.back().c_str();
    }

    std::vector<twEvent>    events_;
    std::deque<std::string> strings_;
};

#endif /* TH_WASM_EVENT_H */
