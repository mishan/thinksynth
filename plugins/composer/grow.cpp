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
 * grow: the instrument's graph changes shape while the piece plays. Every
 * `every' it asks the host for one more edit -- a node spliced into a wire,
 * an envelope or an LFO on a constant, a second voice beside an
 * oscillator, of the kinds `kinds' allows -- or for the last one back, and
 * the channel's graph is rebuilt around the answer. What is sounding
 * finishes on the graph it started on.
 *
 * Like swap, it touches no graph and could not. What it sends is a number
 * from its seed and the words for which edits it allows; the host lists
 * the edits the graph on the channel can take and the number picks one. So
 * a piece that grows replays exactly, and a rewind puts the declared graph
 * back.
 *
 * A walk, not a climb: up to `depth' edits on top of what the piece
 * declares, and between the two a step back with chance `shrink'. A graph
 * that only ever grew would only ever get bigger and busier; one that can
 * go back wanders, and comes home.
 *
 * Given a `target' -- an instrument of the piece or a sound file, as
 * breed's -- the walk leans: the host hears `listen' of the edits that
 * would do and takes the one that sounds nearest. An edit arrives with
 * its plugin's defaults, which are rarely what would bring it nearer, so
 * each is first tuned for `tune' generations -- only in what it added --
 * and judged as tuned. A step back is still the dice's, so the graph
 * drifts toward the target rather than climbing to it. Offline the hearing is
 * done in the tick and a piece replays as the plain walk does; live, as
 * with breed, the step lands on the tick its answers come in.
 */

#include <algorithm>
#include <random>

#include "thcomposer.h"
#include "thcRandom.h"

enum { P_EVERY, P_DEPTH, P_SHRINK, P_KINDS, P_TARGET, P_LISTEN, P_TUNE,
       P_COUNT };

static int paramIndex[P_COUNT];

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "every",  "time between steps", THC_PARAM_FLOAT,
          1, 3600, 20, NULL, "s" },
        { "depth",  "most edits on top of the declared graph",
          THC_PARAM_INT, 1, 16, 4, NULL, NULL },
        { "shrink", "chance of a step back, between none and depth",
          THC_PARAM_FLOAT, 0, 1, 0.3, NULL, NULL },
        { "kinds",  "edits allowed: splice, modulate, voice",
          THC_PARAM_STRING, 0, 0, 0, "splice modulate voice", NULL },
        { "target", "an instrument of the piece, or a sound file, to lean "
          "toward; omit to wander", THC_PARAM_STRING, 0, 0, 0, "", NULL },
        { "listen", "edits heard each step toward `target', the nearest "
          "taken", THC_PARAM_INT, 1, 16, 4, NULL, NULL },
        { "tune",   "generations each edit heard is tuned toward `target' "
          "first; 0 for none", THC_PARAM_INT, 0, 40, 6, NULL, NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_GENERATOR);
    info->set_desc(info->host,
        "Grows its channel's graph an edit at a time, and back.");

    return 0;
}

struct State {
    const thcParams *params;
    std::mt19937     rng;

    /* Edits on top of the declared graph, as this has asked for them. */
    int depth;

    /* The first fire arms the clock and changes nothing, as swap's does:
       a piece opens on the graph it declared. */
    bool started;
};

extern "C" THINK_PLUGIN_API void *
composer_create (const thcParams *params)
{
    State *st = new State;

    st->params = params;
    st->rng.seed(params->seed);
    st->depth = 0;
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

    const int depth = (int)std::min(std::max(p->get(p->ctx,
                                                    paramIndex[P_DEPTH]),
                                             1.0), 16.0);
    const double shrink = p->get(p->ctx, paramIndex[P_SHRINK]);

    /* Drawn whether or not it is used, so changing `shrink' moves which
       steps go back and not which edit each growth picks. */
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    const bool back = uni(st->rng) < shrink;
    const unsigned int choice = (unsigned int)st->rng();

    thcEvent ev = {};

    ev.type = THC_EV_GROW;
    ev.at = t->now;
    ev.channel = 0;                        /* the sink routes            */
    ev.u.grow.choice = choice;
    ev.u.grow.shrink = st->depth >= depth || (st->depth > 0 && back);
    ev.u.grow.kinds = p->get_string(p->ctx, paramIndex[P_KINDS]);
    ev.u.grow.target = p->get_string(p->ctx, paramIndex[P_TARGET]);
    /* Clamped before it is an int: a piece's value is not held to the
       declared range, and a huge double has no int to be. */
    ev.u.grow.listen = (int)std::min(std::max(p->get(p->ctx,
                                                     paramIndex[P_LISTEN]),
                                              1.0), 16.0);
    ev.u.grow.tune = (int)std::min(std::max(p->get(p->ctx, paramIndex[P_TUNE]),
                                            0.0), 40.0);

    st->depth += ev.u.grow.shrink ? -1 : 1;

    out->emit(out->ctx, &ev);

    return next;
}

extern "C" THINK_PLUGIN_API void
composer_destroy (void *state)
{
    delete static_cast<State *>(state);
}
