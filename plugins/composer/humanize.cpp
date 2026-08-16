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

/* humanize -- loosen a note's grip on the grid.
 *
 * The proof that scheduled delivery makes time a value: nudging a note
 * is one line on `at', no clock anywhere in sight. A nudge can land a
 * note slightly in the past of the transport's current instant; the
 * scheduler delivers it on its next tick, which is exactly the small
 * looseness being asked for.
 */

#include <random>

#include "thcomposer.h"

enum { P_TIME, P_VEL, P_COUNT };

static int paramIndex[P_COUNT];

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "time", "+/- nudge on when a note lands", THC_PARAM_FLOAT,
          0, 2, 0.02, NULL, "s" },
        { "vel",  "+/- spread on velocity", THC_PARAM_INT,
          0, 64, 0, NULL, NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_TRANSFORMER);
    info->set_desc(info->host,
        "Nudge timing and velocity off the grid, a little.");

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
composer_receive (void *state, const thcEvent *ev, thcEventSink *out)
{
    State *st = static_cast<State *>(state);
    const thcParams *p = st->params;
    auto get = [&](int i) { return p->get(p->ctx, paramIndex[i]); };

    thcEvent copy = *ev;

    if (copy.type == THC_EV_NOTE)
    {
        std::uniform_real_distribution<double> uni(-1.0, 1.0);

        copy.at += uni(st->rng) * get(P_TIME);

        int vj = (int)get(P_VEL);

        if (vj)
        {
            int v = copy.u.note.velocity +
                    (int)(st->rng() % (2 * vj + 1)) - vj;

            copy.u.note.velocity = v < 1 ? 1 : v > 127 ? 127 : v;
        }
    }

    out->emit(out->ctx, &copy);
}

extern "C" THINK_PLUGIN_API void
composer_destroy (void *state)
{
    delete static_cast<State *>(state);
}
