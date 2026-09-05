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

/* reshape -- past the declared surface, deliberately.
 *
 * The other half of UNIFICATION.md phase 4, at the fine grain. `swap'
 * replaces a channel's whole graph; this changes one constant inside the
 * graph that is already there -- a node's own arg, which the .dsp never
 * offered as a chanarg and which no composer before this could reach.
 *
 * That is the point, and it is worth being plain about. Every composer
 * up to here has been held to the args a patch chose to declare, and
 * COMPOSITION_HANDOFF.md §9 spent a paragraph arguing that the limit was
 * encapsulation rather than a missing feature: "an instrument's mutation
 * surface is what it declares". It also said the way past it would be a
 * *different mechanism* rather than a widening of that one, and this is
 * the different mechanism. A structure edit is not a chanarg with more
 * reach; it is an intent, scheduled, drawn on the roll, replayed from
 * the seed, and written in the piece where a reader can see it.
 *
 * Which means the consent moved rather than vanished. A patch still
 * declares what it wants played with; a piece that reaches deeper has
 * said so out loud in a line anyone can read, and cannot do it faster
 * than its events flow.
 *
 * Sweeps a value between two bounds on a clock -- the simplest thing
 * that proves the path. What makes it interesting is only ever which arg
 * it is pointed at.
 */

#include <cmath>
#include <string>

#include "thcomposer.h"

enum { P_NODE, P_ARG, P_FROM, P_TO, P_EVERY, P_STEPS, P_COUNT };

static int paramIndex[P_COUNT];

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "node",  "a node in the instrument's .dsp", THC_PARAM_STRING,
          0, 0, 0, "", NULL },
        { "arg",   "one of that node's args -- not a chanarg",
          THC_PARAM_STRING, 0, 0, 0, "", NULL },
        { "from",  "value at the start of the sweep", THC_PARAM_FLOAT,
          -100000, 100000, 0, NULL, NULL },
        { "to",    "value at the end of it", THC_PARAM_FLOAT,
          -100000, 100000, 1, NULL, NULL },
        { "every", "time between steps", THC_PARAM_FLOAT,
          0.05, 3600, 4, NULL, "s" },
        { "steps", "how many steps the sweep takes before turning back",
          THC_PARAM_INT, 2, 512, 8, NULL, NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_GENERATOR);
    info->set_desc(info->host,
        "Moves a constant inside the instrument's graph -- one the patch "
        "never declared.");

    return 0;
}

struct State {
    const thcParams *params;
    int              at;
    int              dir;
};

extern "C" THINK_PLUGIN_API void *
composer_create (const thcParams *params)
{
    State *st = new State;

    st->params = params;
    st->at = 0;
    st->dir = 1;

    return st;
}

extern "C" THINK_PLUGIN_API double
composer_tick (void *state, const thcTransport *t, thcEventSink *out)
{
    State *st = static_cast<State *>(state);
    const thcParams *p = st->params;
    auto get = [&](int i) { return p->get(p->ctx, paramIndex[i]); };

    const double every = get(P_EVERY) > 0 ? get(P_EVERY) : 1;

    if (!t->running)
        return t->now + every;

    int steps = (int)get(P_STEPS);

    if (steps < 2)
        steps = 2;

    const char *node = p->get_string(p->ctx, paramIndex[P_NODE]);
    const char *arg  = p->get_string(p->ctx, paramIndex[P_ARG]);

    /* A reshape that names nothing reshapes nothing. Not an error here
       -- the host would say so on every step, which is a log nobody can
       read -- and the loader has already refused an empty one. */
    if (node == NULL || arg == NULL || *node == 0 || *arg == 0)
        return t->now + every;

    /* Turn around one short of the wall, not at it.
     *
       Reflecting at `steps' and at -1 -- which is what the first draft
       did -- means the step after the one that emits `to' emits `to'
       again, and likewise at the bottom: a cycle two ticks longer than
       it should be that stalls for a beat at each extreme. Reflecting
       here, before the value is computed, makes the sweep visit each of
       the `steps' positions exactly once per direction. */
    if (st->at >= steps)
    {
        st->at = steps - 2;
        st->dir = -1;
    }

    if (st->at < 0)
    {
        st->at = 1;
        st->dir = 1;
    }

    /* steps is at least 2, so both of those land inside [0, steps-1] --
       but say it rather than reason about it, because `steps' is a
       param and a param can move between ticks. */
    if (st->at < 0)
        st->at = 0;

    if (st->at >= steps)
        st->at = steps - 1;

    const double f = steps > 1 ? (double)st->at / (double)(steps - 1) : 0;
    const double v = get(P_FROM) + (get(P_TO) - get(P_FROM)) * f;

    thcEvent ev = {};

    ev.type = THC_EV_NODEARG;
    ev.at = t->now;
    ev.channel = 0;                        /* the sink routes            */
    ev.u.nodearg.node = node;
    ev.u.nodearg.arg = arg;
    ev.u.nodearg.value = (float)v;

    out->emit(out->ctx, &ev);

    /* Back and forth rather than round, so a sweep does not jump from
       its top to its bottom in one step -- which on a graph constant is
       audible as a click rather than as a shape. */
    st->at += st->dir;

    return t->now + every;
}

extern "C" THINK_PLUGIN_API void
composer_destroy (void *state)
{
    delete static_cast<State *>(state);
}
