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

/* swing -- the offbeats, late, by a knob.
 *
 * gen::humanize loosens the grid at random; this bends it on purpose.
 * Time is divided into a `grid' -- a sixteenth, usually -- and every
 * note that sits on an odd division is pushed later by `amount' thirds
 * of a division: at 1 the offbeat lands two thirds of the way to the
 * next onbeat, which is the triplet shuffle everybody means by swing;
 * at 0 nothing moves. Notes that are not on the grid at all -- already
 * humanized, or a phrase in some other subdivision -- are left where
 * they are, because a swing that moved them would be a second humanize.
 *
 * The grid is counted from transport zero in the units the file wrote,
 * with the same one-tempo limit xform::form describes. A held note's
 * release moves as far as its press did, so a key held for a sixteenth
 * is still held for a sixteenth.
 *
 * DETERMINISM. A function of the time and the knob; nothing random.
 */

#include <cmath>
#include <map>
#include <vector>

#include "thcomposer.h"

enum { P_GRID, P_AMOUNT, P_COUNT };

static int paramIndex[P_COUNT];

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "grid",   "the division whose offbeats move", THC_PARAM_FLOAT,
          0.01, 60, 0.25, NULL, "s" },
        { "amount", "0 straight; 1 a triplet shuffle", THC_PARAM_FLOAT,
          0, 1, 0.5, NULL, NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_TRANSFORMER);
    info->set_desc(info->host,
        "Push the offbeats of a grid later: shuffle, by a knob.");

    return 0;
}

struct State {
    const thcParams *params;

    /* How far each held pitch's press was moved, oldest first, so its
       release moves the same. */
    std::map<int, std::vector<double> > moved;
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

/* How late a note at `at' goes: a third of a division per unit of
 * amount if it sits on an odd division, else nothing. */
static double
lateness (const State *st, double at)
{
    const thcParams *p = st->params;
    const double grid = p->get(p->ctx, paramIndex[P_GRID]);
    const double amount = p->get(p->ctx, paramIndex[P_AMOUNT]);

    if (grid <= 0 || amount <= 0)
        return 0;

    const double pos = at / grid;
    const double near = floor(pos + 0.5);

    /* On the grid means within a twentieth of a division: wide enough
       for the scheduler's rounding, narrow enough that a humanized note
       is off it. */
    if (fabs(pos - near) > 0.05)
        return 0;

    if (((long)near) % 2 == 0)
        return 0;

    return amount * grid / 3;
}

extern "C" THINK_PLUGIN_API void
composer_receive (void *state, const thcEvent *ev, thcEventSink *out)
{
    State *st = static_cast<State *>(state);
    thcEvent copy = *ev;

    if (ev->type == THC_EV_NOTE)
    {
        const double late = lateness(st, ev->at);

        copy.at += late;

        if (ev->u.note.duration <= 0)
            st->moved[ev->u.note.note].push_back(late);
    }
    else if (ev->type == THC_EV_NOTEOFF)
    {
        std::map<int, std::vector<double> >::iterator it =
            st->moved.find(ev->u.note.note);

        if (it != st->moved.end() && !it->second.empty())
        {
            copy.at += it->second.front();
            it->second.erase(it->second.begin());

            if (it->second.empty())
                st->moved.erase(it);
        }
    }

    out->emit(out->ctx, &copy);
}
