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

/* stutter -- the beat repeat at the end of a phrase, composed.
 *
 * Rides fx/stutter.dsp: `length' before the end of every `period' it
 * puts the effect's hold up and its length at the first of `rolls', moves
 * the length to the next at each equal part of what is left, and puts the
 * hold down as the phrase ends. "0.5 0.25 0.125" over two beats is an
 * eighth, then a sixteenth, then a thirty-second of the same moment: the
 * roll into a drop. delay::stutter takes a shorter length at its next
 * seam, so each step of the roll is the start of the one before.
 *
 * The values name their knobs, `hold' and `beats' after fx/stutter.dsp's,
 * so a chain needs one sink, `chanarg = "*"', on the instrument carrying
 * the effect. `prob' is the chance a phrase ends in one, drawn from the
 * stage's seed; the first `period' starts where the chain does.
 */

#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "thcomposer.h"

enum { P_PERIOD, P_LENGTH, P_ROLLS, P_PROB, P_HOLD, P_BEATS, P_COUNT };

static int paramIndex[P_COUNT];

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "period", "how often: a phrase", THC_PARAM_FLOAT,
          0.05, 600, 8, NULL, "s" },
        { "length", "how long before the phrase ends the repeat starts",
          THC_PARAM_FLOAT, 0.01, 60, 1, NULL, "s" },
        { "rolls", "repeat lengths in beats, one per equal part, in order",
          THC_PARAM_STRING, 0, 0, 0, "0.5 0.25 0.125", NULL },
        { "prob", "the chance a phrase ends in a repeat", THC_PARAM_FLOAT,
          0, 1, 1, NULL, NULL },
        { "hold", "the knob that holds the repeat", THC_PARAM_STRING,
          0, 0, 0, "fx.hold", NULL },
        { "beats", "the knob the repeat's length is in", THC_PARAM_STRING,
          0, 0, 0, "fx.beats", NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_GENERATOR);
    info->set_desc(info->host,
        "A beat repeat rolling shorter at the end of every phrase.");

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

static void
knob (thcEventSink *out, double at, const char *name, float value)
{
    thcEvent ev = {};

    ev.type = THC_EV_CHANARG;
    ev.at = at;
    ev.u.chanarg.name = name;
    ev.u.chanarg.value = value;

    out->emit(out->ctx, &ev);
}

extern "C" THINK_PLUGIN_API double
composer_tick (void *state, const thcTransport *t, thcEventSink *out)
{
    State *st = static_cast<State *>(state);
    const thcParams *p = st->params;
    auto get = [&](int i) { return p->get(p->ctx, paramIndex[i]); };

    const double period = get(P_PERIOD) > 0 ? get(P_PERIOD) : 8;

    /* Drawn whether or not it plays, so a change to `prob' does not move
       the phrases after it. */
    const bool plays =
        std::uniform_real_distribution<double>(0, 1)(st->rng) < get(P_PROB);

    if (!t->running || !plays)
        return t->now + period;

    std::vector<float> rolls;
    const char *text = p->get_string(p->ctx, paramIndex[P_ROLLS]);

    for (char *end = NULL; text && *text; text = end)
    {
        const float v = strtof(text, &end);

        if (end == text)
            break;

        if (v > 0)
            rolls.push_back(v);
    }

    const char *hold = p->get_string(p->ctx, paramIndex[P_HOLD]);
    const char *beats = p->get_string(p->ctx, paramIndex[P_BEATS]);

    if (rolls.empty() || !hold || !*hold || !beats || !*beats)
        return t->now + period;

    /* A param's range is advice, so a length of nothing or less is the
       default's, before it can put the roll after the phrase. */
    double length = get(P_LENGTH) > 0 ? get(P_LENGTH) : 1;

    if (length > period)
        length = period;
    const double from = t->now + period - length;
    const double part = length / rolls.size();

    knob(out, from, beats, rolls[0]);
    knob(out, from, hold, 1);

    for (size_t i = 1; i < rolls.size(); i++)
        knob(out, from + i * part, beats, rolls[i]);

    knob(out, t->now + period, hold, 0);

    return t->now + period;
}
