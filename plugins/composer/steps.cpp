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

/* steps -- a row of values, one per step, to a knob.
 *
 * gen::euclid is a step sequencer of notes; this is one of numbers. A
 * row like "1 0 1 1 0 1 0 0" on a pad's `amp' is a trance gate; a row of
 * cutoffs under a bass is the acid box's step-sequenced filter; a row of
 * levels beside a ring of hats is an accent pattern. Each value is
 * mapped from 0..1 onto `min'..`max' so the row reads as a shape and the
 * knob's own range is said once, and a `_' holds the previous step --
 * nothing is sent, so a slew or a smoothing downstream is not restarted.
 *
 * Written in beats it is a pattern in the bar; in seconds it is a shape
 * that drifts against one, which is airports.gen's argument applied to
 * a knob. The whole row is emitted at once, a row ahead, so the roll
 * shows the shape coming, and the tick sleeps a row.
 *
 * DETERMINISM. Nothing random; the row is the row.
 */

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "thcomposer.h"

enum { P_VALUES, P_PERIOD, P_MIN, P_MAX, P_COUNT };

static int paramIndex[P_COUNT];

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "values", "the row: numbers 0 to 1, _ holds the last",
          THC_PARAM_STRING, 0, 0, 0, "1 0 1 0", NULL },
        { "period", "length of one step", THC_PARAM_FLOAT,
          0.01, 600, 0.25, NULL, "s" },
        { "min",    "what 0 in the row means", THC_PARAM_FLOAT,
          -100000, 100000, 0, NULL, NULL },
        { "max",    "what 1 in the row means", THC_PARAM_FLOAT,
          -100000, 100000, 1, NULL, NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_GENERATOR);
    info->set_desc(info->host,
        "Send a row of values to a knob, one per step, round and round.");

    return 0;
}

struct State {
    const thcParams *params;

    /* The row: a value 0..1, or -1 for a hold. */
    std::vector<double> row;

    void reparse (void);
};

void
State::reparse (void)
{
    const char *s = params->get_string(params->ctx, paramIndex[P_VALUES]);

    row.clear();

    while (s && *s)
    {
        while (*s == ' ' || *s == ',' || *s == '\t')
            s++;

        if (*s == 0)
            break;

        if (*s == '_')
        {
            row.push_back(-1);
            s++;
            continue;
        }

        char *end = NULL;
        double v = strtod(s, &end);

        if (end == s)                    /* not a number: skip the word */
        {
            while (*s && *s != ' ' && *s != ',' && *s != '\t')
                s++;

            continue;
        }

        row.push_back(v < 0 ? 0 : v > 1 ? 1 : v);
        s = end;
    }
}

extern "C" THINK_PLUGIN_API void *
composer_create (const thcParams *params)
{
    State *st = new State;

    st->params = params;
    st->reparse();

    return st;
}

extern "C" THINK_PLUGIN_API void
composer_destroy (void *state)
{
    delete static_cast<State *>(state);
}

extern "C" THINK_PLUGIN_API void
composer_param_changed (void *state, int index)
{
    if (index == paramIndex[P_VALUES])
        static_cast<State *>(state)->reparse();
}

extern "C" THINK_PLUGIN_API double
composer_tick (void *state, const thcTransport *t, thcEventSink *out)
{
    State *st = static_cast<State *>(state);
    const thcParams *p = st->params;
    auto get = [&](int i) { return p->get(p->ctx, paramIndex[i]); };

    const double period = get(P_PERIOD) > 0 ? get(P_PERIOD) : 0.25;

    if (st->row.empty())
        return t->now + period;

    if (t->running)
    {
        const double lo = get(P_MIN), hi = get(P_MAX);

        for (size_t i = 0; i < st->row.size(); i++)
        {
            if (st->row[i] < 0)
                continue;                  /* a hold sends nothing       */

            thcEvent ev = {};

            ev.type = THC_EV_CHANARG;
            ev.at = t->now + i * period;
            ev.channel = 0;                /* the sink routes            */
            ev.u.chanarg.name = NULL;      /* the sink names the target  */
            ev.u.chanarg.value = (float)(lo + st->row[i] * (hi - lo));

            out->emit(out->ctx, &ev);
        }
    }

    return t->now + st->row.size() * period;
}
