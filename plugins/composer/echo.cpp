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
 */

/* echo -- every note again, later and quieter.
 *
 * The oldest trick a console composer had: the second voice plays
 * what the first played a dotted eighth ago, softer, and two channels
 * sound like a hall. overworld.gen did it by hand, with the tune written
 * twice and three rests in front of the second copy. That is a fine
 * lesson once and a chore every time after; this is the chore done.
 *
 * Each note that arrives goes out again `repeats' times, `time' apart,
 * its velocity multiplied by `decay' each time -- a repeat that would
 * be too quiet to matter is not sent -- and moved `shift' semitones
 * each time, which is zero unless a piece wants the chip's other
 * cliche, the echo an octave down. `pass' is whether the note itself
 * goes on as well: off, and this stage is only the echoes, which is how
 * a second instrument gets them on its own channel.
 *
 * COMPOSITION_HANDOFF.md §3b called an echo "emit future copies", and it
 * is: no clock, no buffer, a copy of the event with a later `at'. What
 * makes it more than one line is a held note, whose repeats are held
 * too and need releasing when the key comes up: the release goes out
 * `repeats' times as well, each as late as its on was.
 *
 * DETERMINISM. Nothing random; the copies are arithmetic.
 */

#include <cmath>
#include <map>
#include <utility>
#include <vector>

#include "thcomposer.h"

enum { P_REPEATS, P_TIME, P_DECAY, P_SHIFT, P_PASS, P_COUNT };

static int paramIndex[P_COUNT];

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "repeats", "how many times a note comes back", THC_PARAM_INT,
          1, 16, 3, NULL, NULL },
        { "time",    "time between repeats", THC_PARAM_FLOAT,
          0.01, 60, 0.25, NULL, "s" },
        { "decay",   "velocity multiplier per repeat", THC_PARAM_FLOAT,
          0, 1, 0.6, NULL, NULL },
        { "shift",   "semitones each repeat moves", THC_PARAM_INT,
          -24, 24, 0, NULL, NULL },
        { "pass",    "1: the note itself is heard too; 0: only the echoes",
          THC_PARAM_INT, 0, 1, 1, NULL, NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_TRANSFORMER);
    info->set_desc(info->host,
        "Repeat every note, later and quieter, like a delay line.");

    return 0;
}

struct State {
    const thcParams *params;

    /* For a held note: the pitch and delay of every repeat still down,
       keyed by the pitch that was pressed. */
    std::map<int, std::vector<std::pair<int, double> > > held;
};

extern "C" THINK_PLUGIN_API void *
composer_create (const thcParams *params)
{
    State *st = new State;

    st->params = params;

    return st;
}

extern "C" THINK_PLUGIN_API void
composer_destroy (void *state)
{
    delete static_cast<State *>(state);
}

extern "C" THINK_PLUGIN_API void
composer_receive (void *state, const thcEvent *ev, thcEventSink *out)
{
    State *st = static_cast<State *>(state);
    const thcParams *p = st->params;
    auto get = [&](int i) { return p->get(p->ctx, paramIndex[i]); };

    if (ev->type == THC_EV_NOTEOFF)
    {
        if ((int)get(P_PASS))
            out->emit(out->ctx, ev);

        std::map<int, std::vector<std::pair<int, double> > >::iterator it =
            st->held.find(ev->u.note.note);

        if (it == st->held.end())
            return;

        for (size_t i = 0; i < it->second.size(); i++)
        {
            thcEvent off = *ev;

            off.u.note.note = it->second[i].first;
            off.at = ev->at + it->second[i].second;
            out->emit(out->ctx, &off);
        }

        st->held.erase(it);
        return;
    }

    if (ev->type != THC_EV_NOTE)
    {
        out->emit(out->ctx, ev);
        return;
    }

    if ((int)get(P_PASS))
        out->emit(out->ctx, ev);

    const int    repeats = (int)get(P_REPEATS);
    const double time    = get(P_TIME);
    const double decay   = get(P_DECAY);
    const int    shift   = (int)get(P_SHIFT);
    const bool   isHeld  = ev->u.note.duration <= 0;

    std::vector<std::pair<int, double> > down;
    double vel = ev->u.note.velocity;

    for (int i = 1; i <= repeats; i++)
    {
        vel *= decay;

        const int v = (int)floor(vel + 0.5);
        const int note = ev->u.note.note + i * shift;

        if (v < 1)
            break;                       /* and every later one quieter */

        if (note < 0 || note > 127)
            continue;

        thcEvent copy = *ev;

        copy.at = ev->at + i * time;
        copy.u.note.note = note;
        copy.u.note.velocity = v;

        out->emit(out->ctx, &copy);

        if (isHeld)
            down.push_back(std::make_pair(note, i * time));
    }

    if (isHeld)
        st->held[ev->u.note.note] = down;
}
