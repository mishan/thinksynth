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

/* progression -- a walk over the chords of a key.
 *
 * The generative plugins in the tree are all worst at the same thing:
 * harmony. A markov chain, a GA and an automaton each pick pitches,
 * and pitches picked one at a time are dissonant more often than a
 * person would be. This picks *chords*, and it picks them the way
 * common practice does: from a table of where each degree of a key
 * likes to go next -- the tonic to anywhere, the supertonic to the
 * dominant, the dominant home -- with a phrase length that forces a
 * cadence. The first chord of every phrase is the tonic and the last
 * is the dominant, so however the middle wanders the phrase ends
 * wanting to go home and the next one does.
 *
 * What comes out is one root per chord, in the octave the scale was
 * written in. It is a root and not a chord on purpose: xform::harmonize
 * downstream spells the chord from the same scale, so the quality is
 * the scale's business, and xform::bassline turns the same root into a
 * bass. `wander' is how often the table is ignored for any degree at
 * all; at 0 the walk is strictly the table's.
 *
 * TWO CHAINS, ONE WALK. A piece wants the chords on one instrument and
 * the bass on another, which is two chains, and two stages draw
 * different seeds from the piece's one -- so `seed' may be set, and
 * two stages with the same one walk the same progression without a
 * message passing between them. That is orrery.gen's trick, of a bass
 * that agrees with the chords because both read the same table, with
 * the table replaced by a die everybody rolls the same way. At 0 the
 * instance seed is used, as everywhere else.
 *
 * The table is for seven degrees. A scale with fewer -- a pentatonic --
 * folds it, and a scale with more reads the extra notes as degrees the
 * table never visits.
 *
 * DETERMINISM. Every draw is from the seed; the same piece walks the
 * same chords twice.
 */

#include <cstdlib>
#include <cstring>
#include <random>

#include "thcomposer.h"
#include "thcRandom.h"

enum { P_SCALE, P_EVERY, P_HOLD, P_VEL, P_PHRASE, P_WANDER, P_SEED,
       P_COUNT };

static int paramIndex[P_COUNT];

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "scale",  "the key, one octave, tonic first",
          THC_PARAM_NOTESET, 0, 0, 0, "48,50,52,53,55,57,59", NULL },
        { "every",  "time between chords", THC_PARAM_FLOAT,
          0.05, 600, 2, NULL, "s" },
        { "hold",   "time before the root's note-off", THC_PARAM_FLOAT,
          0.01, 600, 1.9, NULL, "s" },
        { "vel",    "velocity", THC_PARAM_INT, 1, 127, 80, NULL, NULL },
        { "phrase", "chords per phrase; the first is I, the last is V",
          THC_PARAM_INT, 1, 32, 4, NULL, NULL },
        { "wander", "chance a chord ignores the table", THC_PARAM_FLOAT,
          0, 1, 0.1, NULL, NULL },
        { "seed",   "0: the instance's; else this, shared with another "
          "stage", THC_PARAM_INT, 0, 999999, 0, NULL, NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_GENERATOR);
    info->set_desc(info->host,
        "Walk the chords of a key, with a cadence at the end of every "
        "phrase; emits roots.");

    return 0;
}

/* Where each degree likes to go: weights, row by row, I to vii. The
 * numbers are a textbook's, roughly -- what matters is that the
 * dominant goes home and the tonic goes anywhere. */
static const int table[7][7] = {
    /*        I  ii iii IV  V  vi vii */
    /* I   */ { 0, 2, 1, 3, 3, 3, 0 },
    /* ii  */ { 0, 0, 0, 1, 4, 0, 1 },
    /* iii */ { 0, 0, 0, 2, 0, 3, 0 },
    /* IV  */ { 2, 2, 0, 0, 3, 1, 0 },
    /* V   */ { 4, 0, 0, 1, 0, 2, 0 },
    /* vi  */ { 0, 2, 1, 3, 2, 0, 0 },
    /* vii */ { 3, 0, 1, 0, 0, 0, 0 },
};

struct State {
    const thcParams *params;
    std::mt19937     rng;

    int pool[128];
    int poolLen;

    int count;                  /* chords emitted so far                */
    int prev;                   /* degree of the last one               */

    void reparse (void);
    void reseed (void);
};

void
State::reparse (void)
{
    const char *s = params->get_string(params->ctx, paramIndex[P_SCALE]);

    poolLen = 0;

    while (s && *s && poolLen < 128)
    {
        int n = atoi(s);

        if (n >= 0 && n <= 127)
            pool[poolLen++] = n;

        if ((s = strchr(s, ',')))
            s++;
    }
}

void
State::reseed (void)
{
    const int own = (int)params->get(params->ctx, paramIndex[P_SEED]);

    rng.seed(own > 0 ? (unsigned)own : params->seed);
    count = 0;
    prev = 0;
}

extern "C" THINK_PLUGIN_API void *
composer_create (const thcParams *params)
{
    State *st = new State;

    st->params = params;
    st->reparse();
    st->reseed();

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
    State *st = static_cast<State *>(state);

    if (index == paramIndex[P_SCALE])
        st->reparse();

    if (index == paramIndex[P_SEED])
        st->reseed();
}

/* The next degree after `from', by the table, or anywhere at all. */
static int
next (State *st, int from)
{
    const thcParams *p = st->params;
    std::uniform_real_distribution<double> uni(0.0, 1.0);

    if (uni(st->rng) < p->get(p->ctx, paramIndex[P_WANDER]))
        return (int)thcUniformIndex(st->rng, 0, 6);

    int total = 0;

    for (int i = 0; i < 7; i++)
        total += table[from][i];

    if (total <= 0)
        return 0;

    int r = (int)thcUniformIndex(st->rng, 0, (size_t)total - 1);

    for (int i = 0; i < 7; i++)
    {
        r -= table[from][i];

        if (r < 0)
            return i;
    }

    return 0;
}

extern "C" THINK_PLUGIN_API double
composer_tick (void *state, const thcTransport *t, thcEventSink *out)
{
    State *st = static_cast<State *>(state);
    const thcParams *p = st->params;
    auto get = [&](int i) { return p->get(p->ctx, paramIndex[i]); };

    if (t->running && st->poolLen > 0)
    {
        const int phrase = (int)get(P_PHRASE) < 1 ? 1 : (int)get(P_PHRASE);
        const int k = st->count % phrase;
        int degree;

        if (k == 0)
            degree = 0;                         /* the tonic            */
        else if (k == phrase - 1)
            degree = 4;                         /* the dominant         */
        else
            degree = next(st, st->prev);

        thcEvent ev = {};

        ev.type = THC_EV_NOTE;
        ev.at = t->now;
        ev.channel = 0;                          /* the sink routes     */
        ev.u.note.note = st->pool[degree % st->poolLen];
        ev.u.note.velocity = (int)get(P_VEL);
        ev.u.note.duration = get(P_HOLD);

        out->emit(out->ctx, &ev);

        st->prev = degree;
        st->count++;
    }

    return t->now + get(P_EVERY);
}
