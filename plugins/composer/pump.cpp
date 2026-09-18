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

/* pump -- the sidechain, as a knob that ducks on the beat.
 *
 * The pad that breathes under a four-on-the-floor kick is a compressor
 * keyed from the kick's channel, and this engine has no way to build
 * one: a voice is its own tree and a channel cannot hear another. So the
 * duck is composed instead. Once a `period' this emits a dip in a knob
 * -- to `level' times (1 - depth) at the top of the cycle, held for
 * `hold', then back up to `level' over `rise' -- as `steps' events across
 * the cycle. Aimed at a pad's `amp', it is the pumping; aimed at a
 * filter's cutoff, it is the other thing a sidechain does.
 *
 * `curve' shapes the way back: 1 is a straight line, above it the knob
 * comes back fast and then eases, which is what a compressor's release
 * sounds like. The cycle starts at transport zero, where the kick is.
 *
 * `hold' plus `rise' longer than `period' is a duck the knob never
 * finishes climbing out of: the cycle restarts from wherever it had got
 * to, so the knob steps down rather than being ducked. That is a
 * legitimate thing to ask for -- it is a pad held under -- but it is not
 * a sidechain, and the two are one number apart.
 *
 * The whole cycle is emitted at once and the tick sleeps a cycle, like
 * gen::steps. Nothing random.
 */

#include <cmath>
#include <cstddef>

#include "thcomposer.h"

enum { P_PERIOD, P_DEPTH, P_HOLD, P_RISE, P_LEVEL, P_STEPS, P_CURVE,
       P_COUNT };

static int paramIndex[P_COUNT];

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "period", "time between ducks", THC_PARAM_FLOAT,
          0.05, 60, 0.5, NULL, "s" },
        { "depth",  "how far the knob dips, as a fraction of level",
          THC_PARAM_FLOAT, 0, 1, 0.6, NULL, NULL },
        { "hold",   "time at the bottom of the dip", THC_PARAM_FLOAT,
          0, 10, 0.02, NULL, "s" },
        { "rise",   "time back up to level", THC_PARAM_FLOAT,
          0.01, 60, 0.25, NULL, "s" },
        { "level",  "the knob's value between ducks", THC_PARAM_FLOAT,
          -100000, 100000, 32, NULL, NULL },
        { "steps",  "events per cycle", THC_PARAM_INT,
          2, 64, 16, NULL, NULL },
        { "curve",  "1 comes back straight; above hurries then eases",
          THC_PARAM_FLOAT, 0.25, 8, 1.5, NULL, NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_GENERATOR);
    info->set_desc(info->host,
        "Duck a knob on every beat and let it back up: a sidechain, "
        "composed.");

    return 0;
}

struct State {
    const thcParams *params;
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

extern "C" THINK_PLUGIN_API double
composer_tick (void *state, const thcTransport *t, thcEventSink *out)
{
    State *st = static_cast<State *>(state);
    const thcParams *p = st->params;
    auto get = [&](int i) { return p->get(p->ctx, paramIndex[i]); };

    const double period = get(P_PERIOD) > 0 ? get(P_PERIOD) : 0.5;

    if (t->running)
    {
        const double depth = get(P_DEPTH);
        const double hold  = get(P_HOLD);
        const double rise  = get(P_RISE) > 0 ? get(P_RISE) : 0.01;
        const double level = get(P_LEVEL);
        const double curve = get(P_CURVE);
        const int    steps = (int)get(P_STEPS) < 2 ? 2 : (int)get(P_STEPS);

        for (int i = 0; i < steps; i++)
        {
            const double tau = i * period / steps;
            double dip;                    /* 0 at the bottom, 1 back up */

            if (tau < hold)
                dip = 0;
            else if (tau < hold + rise)
                dip = 1 - pow(1 - (tau - hold) / rise, curve);
            else
                dip = 1;

            thcEvent ev = {};

            ev.type = THC_EV_CHANARG;
            ev.at = t->now + tau;
            ev.channel = 0;                /* the sink routes            */
            ev.u.chanarg.name = NULL;      /* the sink names the target  */
            ev.u.chanarg.value = (float)(level * (1 - depth * (1 - dip)));

            out->emit(out->ctx, &ev);
        }
    }

    return t->now + period;
}
