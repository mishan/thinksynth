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

/* ratchet -- one note becomes a burst of them.
 *
 * A snare hit that is really three, a hat that stutters: the roll a
 * drum machine calls a ratchet or a flam. Each note that arrives is,
 * with probability `prob', replaced by `count' notes that share its
 * time -- the first where it was, the rest evenly across its duration,
 * each `decay' times as loud as the one before. The burst is exactly
 * as long as the note it replaces, so a ratcheted sixteenth is still a
 * sixteenth.
 *
 * Only a note with a duration can be divided; a held note has no
 * length yet, and goes through as it came. The draws come from the
 * instance seed, so the rolls fall in the same places on every replay.
 */

#include <cstddef>
#include <cmath>
#include <random>

#include "thcomposer.h"

enum { P_COUNT_, P_PROB, P_DECAY, P_COUNT };

static int paramIndex[P_COUNT];

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "count", "notes in a burst", THC_PARAM_INT,
          2, 8, 3, NULL, NULL },
        { "prob",  "chance a note becomes a burst", THC_PARAM_FLOAT,
          0, 1, 0.25, NULL, NULL },
        { "decay", "velocity multiplier per note of the burst",
          THC_PARAM_FLOAT, 0.1, 1, 0.85, NULL, NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_TRANSFORMER);
    info->set_desc(info->host,
        "Turn some notes into bursts of shorter ones: rolls and flams.");

    return 0;
}

struct State {
    const thcParams *params;
    std::mt19937     rng;
};

extern "C" THINK_PLUGIN_API void *
composer_create (const thcParams *params)
{
    State *st = new State;

    st->params = params;
    st->rng.seed(params->seed);

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

    if (ev->type != THC_EV_NOTE || ev->u.note.duration <= 0)
    {
        out->emit(out->ctx, ev);
        return;
    }

    std::uniform_real_distribution<double> uni(0.0, 1.0);

    if (uni(st->rng) >= get(P_PROB))
    {
        out->emit(out->ctx, ev);
        return;
    }

    const int    count = (int)get(P_COUNT_);
    const double decay = get(P_DECAY);
    const double each  = ev->u.note.duration / count;

    double vel = ev->u.note.velocity;

    for (int i = 0; i < count; i++)
    {
        thcEvent copy = *ev;
        const int v = (int)floor(vel + 0.5);

        copy.at = ev->at + i * each;
        copy.u.note.duration = each;
        copy.u.note.velocity = v < 1 ? 1 : v;

        out->emit(out->ctx, &copy);

        vel *= decay;
    }
}
