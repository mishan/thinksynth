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
 */

/*
 * graft: a population of the instrument's graphs, bred while the piece
 * plays. Every `every' is a generation: the host scores the flock, puts
 * the best on the channel -- what is sounding finishes on the graph it
 * started on -- and breeds the next, crossing two graphs by the edits
 * each made to the declared one and giving a child one edit more.
 *
 * What breed does to a patch's values this does to its shape, and like
 * grow it touches no graph and could not: it sends the generation's dice
 * and how to breed, and the flock is the host's. So a piece that grafts
 * replays exactly, and a rewind puts the declared graph back.
 *
 * Given a `target' -- an instrument of the piece or a sound file -- the
 * flock is scored by how near each sounds to it, each new edit tuned
 * toward it first: a population climbing. With none, by how far each
 * sounds from the champion just played, up to a point, so the
 * instrument keeps changing, each new edit tuned for that and none let
 * stray far from the declared graph's brightness or low end.
 */

#include <random>

#include "thcomposer.h"

enum { P_EVERY, P_POPULATION, P_ELITES, P_CROSSOVER, P_MUTATION, P_DEPTH,
       P_KINDS, P_TARGET, P_TUNE, P_COUNT };

static int paramIndex[P_COUNT];

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "every",      "time between generations", THC_PARAM_FLOAT,
          1, 3600, 20, NULL, "s" },
        { "population", "graphs in the flock", THC_PARAM_INT,
          2, 16, 6, NULL, NULL },
        { "elites",     "the best carried to the next generation as they "
          "are", THC_PARAM_INT, 0, 4, 1, NULL, NULL },
        { "crossover",  "chance a child has two parents", THC_PARAM_FLOAT,
          0, 1, 0.7, NULL, NULL },
        { "mutation",   "chance a child takes one edit more",
          THC_PARAM_FLOAT, 0, 1, 0.8, NULL, NULL },
        { "depth",      "most edits on top of the declared graph",
          THC_PARAM_INT, 0, 16, 4, NULL, NULL },
        { "kinds",      "edits allowed: splice, modulate, voice",
          THC_PARAM_STRING, 0, 0, 0, "splice modulate voice", NULL },
        { "target",     "an instrument of the piece, or a sound file, to "
          "breed toward; omit to keep changing", THC_PARAM_STRING,
          0, 0, 0, "", NULL },
        { "tune",       "generations each new edit is tuned before it "
          "is judged; 0 for none", THC_PARAM_INT, 0, 40, 6, NULL,
          NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_GENERATOR);
    info->set_desc(info->host,
        "Breeds a population of its channel's graph, the best played.");

    return 0;
}

struct State {
    const thcParams *params;
    std::mt19937     rng;

    /* The first fire arms the clock and changes nothing: a piece opens
       on the graph it declared. */
    bool started;
};

extern "C" THINK_PLUGIN_API void *
composer_create (const thcParams *params)
{
    State *st = new State;

    st->params = params;
    st->rng.seed(params->seed);
    st->started = false;

    return st;
}

extern "C" THINK_PLUGIN_API double
composer_tick (void *state, const thcTransport *t, thcEventSink *out)
{
    State *st = static_cast<State *>(state);
    const thcParams *p = st->params;
    const double every = p->get(p->ctx, paramIndex[P_EVERY]);
    const double next = t->now + (every > 0 ? every : 1);

    if (!t->running)
        return next;

    if (!st->started)
    {
        st->started = true;
        return next;
    }

    thcEvent ev = {};

    ev.type = THC_EV_GRAFT;
    ev.at = t->now;
    ev.channel = 0;                        /* the sink routes            */
    ev.u.graft.dice = (unsigned int)st->rng();
    ev.u.graft.kinds = p->get_string(p->ctx, paramIndex[P_KINDS]);
    ev.u.graft.target = p->get_string(p->ctx, paramIndex[P_TARGET]);
    ev.u.graft.population = thcParamInt(p, paramIndex[P_POPULATION], 2, 16);
    ev.u.graft.elites = thcParamInt(p, paramIndex[P_ELITES], 0, 4);
    ev.u.graft.crossover = (float)p->get(p->ctx, paramIndex[P_CROSSOVER]);
    ev.u.graft.mutation = (float)p->get(p->ctx, paramIndex[P_MUTATION]);
    ev.u.graft.depth = thcParamInt(p, paramIndex[P_DEPTH], 0, 16);
    ev.u.graft.tune = thcParamInt(p, paramIndex[P_TUNE], 0, 40);

    out->emit(out->ctx, &ev);

    return next;
}

extern "C" THINK_PLUGIN_API void
composer_destroy (void *state)
{
    delete static_cast<State *>(state);
}
