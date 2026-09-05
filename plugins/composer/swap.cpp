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

/* swap -- the instrument itself becomes the material.
 *
 * UNIFICATION.md phase 4, at its coarsest and most useful grain: a
 * composer that changes *what the channel is*, not what it is playing.
 * Every other composer in this directory emits notes or knob values;
 * this one emits an intent to rebuild a channel around a different
 * graph, and the host does it through the same patch-load path a person
 * clicking in the Patch Selector uses.
 *
 * It touches no graph and could not: a composer cannot link libthink,
 * and the only thing it knows about an instrument is its name -- handed
 * over already resolved, in the list the piece declared, exactly as a
 * scale arrives as numbers and a preset as a vector. That is the whole
 * of the ABI's bargain, and it is what keeps a plugin from ever holding
 * a pointer into somebody's synth.
 *
 * Being an event is the rate limit. A swap is scheduled, sparse, drawn
 * on the roll, and replayed from the seed like everything else -- so
 * this cannot thrash a channel faster than the event stream flows, and
 * a piece that swaps sounds the same twice.
 */

#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "thcomposer.h"

enum { P_INSTRUMENTS, P_EVERY, P_ORDER, P_COUNT };

static int paramIndex[P_COUNT];

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "instruments", "which of the piece's instruments to move between",
          THC_PARAM_INSTRSET, 0, 0, 0, "", NULL },
        { "every",       "time between swaps", THC_PARAM_FLOAT,
          1, 3600, 30, NULL, "s" },
        { "order",       "0 round-robin, 1 picked from the seed",
          THC_PARAM_INT, 0, 1, 0, NULL, NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_GENERATOR);
    info->set_desc(info->host,
        "Rebuilds its channel around a different instrument, on a clock.");

    return 0;
}

struct State {
    const thcParams *params;
    std::mt19937     rng;

    /* The resolved list, split once. Re-split when the param changes,
       which is what composer_param_changed is for -- an editor moving
       the list mid-piece should not need a reload. */
    std::vector<std::string> names;
    std::string              raw;

    size_t at;

    /* The first fire arms the clock and swaps nothing: a piece opens on
       the instrument it declared, and a swap at t=0 would replace it
       before a note had sounded -- which reads as "the file's own
       instrument is never heard". */
    bool started;
};

static void
resplit (State *st)
{
    const thcParams *p = st->params;
    const char *text = p->get_string(p->ctx, paramIndex[P_INSTRUMENTS]);

    if (text == NULL)
        text = "";

    if (st->raw == text)
        return;

    st->raw = text;
    st->names.clear();

    std::string one;

    /* Whitespace separates as a comma does, which is what thcGenLoader
       decided at the file boundary and what this has to agree with.
       A loader-normalised list arrives as "pad,bell" and would not care;
       the param panel is the other writer, and it stores what was typed
       -- so "pad, bell" reached here as a name with a space welded to
       the front of it, and every swap to it was refused by a service
       that had never heard of " bell". A separator is a separator. */
    for (size_t i = 0; i <= st->raw.size(); i++)
    {
        const char c = i < st->raw.size() ? st->raw[i] : ',';

        if (c == ',' || c == ' ' || c == '\t' || c == '\n' || c == '\r')
        {
            if (!one.empty())
                st->names.push_back(one);
            one.clear();
        }
        else
            one += c;
    }

    if (st->at >= st->names.size())
        st->at = 0;
}

extern "C" THINK_PLUGIN_API void *
composer_create (const thcParams *params)
{
    State *st = new State;

    st->params = params;
    st->rng.seed(params->seed);
    st->at = 0;
    st->started = false;

    resplit(st);

    return st;
}

extern "C" THINK_PLUGIN_API void
composer_param_changed (void *state, int index)
{
    if (index == paramIndex[P_INSTRUMENTS])
        resplit(static_cast<State *>(state));
}

extern "C" THINK_PLUGIN_API double
composer_tick (void *state, const thcTransport *t, thcEventSink *out)
{
    State *st = static_cast<State *>(state);
    const thcParams *p = st->params;
    const double every = p->get(p->ctx, paramIndex[P_EVERY]);

    resplit(st);

    /* Nothing to move between is not an error, it is a piece that named
       one instrument and meant it. Sleep on the clock rather than
       spinning: the next param change re-arms. */
    if (st->names.size() < 2 || !t->running)
        return t->now + (every > 0 ? every : 1);

    if (!st->started)
    {
        st->started = true;
        return t->now + (every > 0 ? every : 1);
    }

    if ((int)p->get(p->ctx, paramIndex[P_ORDER]) == 1)
    {
        /* Picked, but never the one already sounding -- a swap to what
           is already there is an event that rebuilds a channel to look
           exactly as it did, which is the one outcome nobody wanted. */
        std::uniform_int_distribution<size_t> pick(0, st->names.size() - 2);
        size_t n = pick(st->rng);

        st->at = n < st->at ? n : n + 1;
    }
    else
        st->at = (st->at + 1) % st->names.size();

    thcEvent ev = {};

    ev.type = THC_EV_PATCH;
    ev.at = t->now;
    ev.channel = 0;                        /* the sink routes            */
    ev.u.patch.name = st->names[st->at].c_str();

    out->emit(out->ctx, &ev);

    return t->now + (every > 0 ? every : 1);
}

extern "C" THINK_PLUGIN_API void
composer_destroy (void *state)
{
    delete static_cast<State *>(state);
}
