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

/* walk -- a bounded random walk, emitted as chanarg values.
 *
 * Generative timbre as a first-class citizen: point a chanarg sink at a
 * patch knob and this drifts it -- a filter opening over minutes, a
 * detune breathing. The walk reflects off its bounds rather than
 * clamping to them, so it does not stick to an edge the way a clamped
 * walk does.
 */

#include <random>

#include "thcomposer.h"

enum { P_MIN, P_MAX, P_STEP, P_PERIOD, P_COUNT };

static int paramIndex[P_COUNT];

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "min",    "lower bound of the walk", THC_PARAM_FLOAT,
          0, 1, 0, NULL, NULL },
        { "max",    "upper bound of the walk", THC_PARAM_FLOAT,
          0, 1, 1, NULL, NULL },
        { "step",   "largest move per firing", THC_PARAM_FLOAT,
          0, 1, 0.05, NULL, NULL },
        { "period", "time between firings", THC_PARAM_FLOAT,
          0.05, 600, 8, NULL, "s" },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_GENERATOR);
    info->set_desc(info->host,
        "A bounded random walk, delivered to a patch knob.");

    return 0;
}

struct State {
    const thcParams *params;
    std::mt19937     rng;
    double           value;
    bool             seeded;   /* start at mid-range on the first tick   */
};

extern "C" THINK_PLUGIN_API void *
composer_create (const thcParams *params)
{
    State *st = new State;

    st->params = params;
    st->rng.seed(params->seed);
    st->value = 0;
    st->seeded = false;

    return st;
}

extern "C" THINK_PLUGIN_API double
composer_tick (void *state, const thcTransport *t, thcEventSink *out)
{
    State *st = static_cast<State *>(state);
    const thcParams *p = st->params;
    auto get = [&](int i) { return p->get(p->ctx, paramIndex[i]); };

    double lo = get(P_MIN), hi = get(P_MAX);

    if (hi < lo) { double sw = lo; lo = hi; hi = sw; }

    if (!st->seeded)
    {
        st->value = (lo + hi) / 2;
        st->seeded = true;
    }

    if (t->running)
    {
        std::uniform_real_distribution<double> uni(-1.0, 1.0);

        st->value += uni(st->rng) * get(P_STEP);

        /* reflect, not clamp */
        if (st->value > hi)
            st->value = hi - (st->value - hi);
        if (st->value < lo)
            st->value = lo + (lo - st->value);
        if (st->value < lo || st->value > hi)   /* a step wider than the
                                                   whole range */
            st->value = (lo + hi) / 2;

        thcEvent ev = {};

        ev.type = THC_EV_CHANARG;
        ev.at = t->now;
        ev.channel = 0;                    /* the sink routes            */
        ev.u.chanarg.name = NULL;          /* the sink names the target  */
        ev.u.chanarg.value = (float)st->value;

        out->emit(out->ctx, &ev);
    }

    return t->now + get(P_PERIOD);
}

extern "C" THINK_PLUGIN_API void
composer_destroy (void *state)
{
    delete static_cast<State *>(state);
}
