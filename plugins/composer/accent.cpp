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

/* accent -- velocity by where a note falls.
 *
 * Every generator in the tree plays at one velocity unless a row was
 * written for it, and a row on a chanarg -- gen::steps into an `amp' --
 * moves the whole channel rather than the note. What is missing is the
 * thing a drummer does without thinking: lean on the ones and the
 * threes, and lift through the bar.
 *
 * Two independent shapes, multiplied into the velocity:
 *
 *   a PATTERN over a `grid'. One mark per step, `x' for the ones to
 *   lean on: "x..x..x." over a sixteenth grid is the tresillo, played
 *   at `strong' where the marks are and `weak' everywhere else. The
 *   pattern repeats for ever.
 *
 *   a SWELL over a `bar'. The velocity is scaled from `from' at the top
 *   of the bar to `to' at the end of it, straight through -- so a phrase
 *   can lift into the next one, or fall away from it.
 *
 * Both are off by default (an empty pattern, and `from' and `to' both
 * 1), so a stage nobody has asked anything of passes every note
 * through at the velocity it came in with.
 *
 * WHICH STEP a note belongs to is the step its time falls in, not the
 * nearest one: a note nudged by humanize is still the note on the beat,
 * and should be leaned on as one. That is the opposite of xform::swing,
 * which asks to be *on* the grid before it moves anything, because there
 * the answer is a movement and here it is a weight.
 *
 * The grid and the bar are counted from transport zero in the units the
 * file wrote, with the one-tempo limit xform::form describes at length.
 *
 * DETERMINISM. A function of the time and the two shapes; nothing
 * random. Velocity is the only thing this touches, so an off needs no
 * bookkeeping: a release names a pitch, not a loudness.
 */

#include <cstddef>
#include <cmath>
#include <cstring>

#include "thcomposer.h"

enum { P_PATTERN, P_GRID, P_STRONG, P_WEAK, P_BAR, P_FROM, P_TO,
       P_COUNT };

static int paramIndex[P_COUNT];

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "pattern", "one mark per step: x is an accent", THC_PARAM_STRING,
          0, 0, 0, "", NULL },
        { "grid",    "length of one step of the pattern", THC_PARAM_FLOAT,
          0.01, 60, 0.25, NULL, "s" },
        { "strong",  "velocity multiplier on a marked step",
          THC_PARAM_FLOAT, 0, 4, 1.15, NULL, NULL },
        { "weak",    "velocity multiplier everywhere else",
          THC_PARAM_FLOAT, 0, 4, 0.85, NULL, NULL },
        { "bar",     "length the swell runs over", THC_PARAM_FLOAT,
          0.05, 600, 2, NULL, "s" },
        { "from",    "swell multiplier at the top of the bar",
          THC_PARAM_FLOAT, 0, 4, 1, NULL, NULL },
        { "to",      "swell multiplier at the end of the bar",
          THC_PARAM_FLOAT, 0, 4, 1, NULL, NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_TRANSFORMER);
    info->set_desc(info->host,
        "Weight each note by where it falls: an accent pattern and a swell.");

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

/* What the pattern says about a note at `at': 1 when there is no
 * pattern, so the two shapes are independent and either can be used
 * alone. */
static double
weight (const State *st, double at)
{
    const thcParams *p = st->params;
    const char *pattern = p->get_string(p->ctx, paramIndex[P_PATTERN]);
    const double grid = p->get(p->ctx, paramIndex[P_GRID]);

    const int len = pattern ? (int)strlen(pattern) : 0;

    if (len == 0 || grid <= 0)
        return 1;

    /* The same nudge form makes at a bar line: a note written on the
       step belongs to the step it opens. */
    long n = (long)floor(at / grid + 1e-6);

    if (n < 0)
        n = 0;

    n %= len;

    return pattern[n] == 'x' || pattern[n] == 'X'
        ? p->get(p->ctx, paramIndex[P_STRONG])
        : p->get(p->ctx, paramIndex[P_WEAK]);
}

/* And what the swell says: straight from `from' to `to' across the bar,
 * and 1 when the two are equal, which is the default and is what makes
 * this cost nothing to leave alone. */
static double
swell (const State *st, double at)
{
    const thcParams *p = st->params;
    const double bar = p->get(p->ctx, paramIndex[P_BAR]);
    const double from = p->get(p->ctx, paramIndex[P_FROM]);
    const double to = p->get(p->ctx, paramIndex[P_TO]);

    if (from == to)
        return from;                  /* the default is 1, and free  */

    if (bar <= 0)
        return 1;

    double phase = fmod(at < 0 ? 0 : at, bar) / bar;

    if (phase < 0)
        phase = 0;

    return from + (to - from) * phase;
}

extern "C" THINK_PLUGIN_API void
composer_receive (void *state, const thcEvent *ev, thcEventSink *out)
{
    State *st = static_cast<State *>(state);

    if (ev->type != THC_EV_NOTE)
    {
        out->emit(out->ctx, ev);
        return;
    }

    thcEvent copy = *ev;
    const double v = ev->u.note.velocity * weight(st, ev->at) *
                     swell(st, ev->at);
    const int i = (int)floor(v + 0.5);

    /* Quieter or louder, never absent: a stage that weights a line
       still plays every note of it. Dropping one is what xform::chance
       is for, and a velocity of nothing would be a note deleted by a
       multiplication. */
    copy.u.note.velocity = i < 1 ? 1 : i > 127 ? 127 : i;

    out->emit(out->ctx, &copy);
}
