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

/* markov -- learn what it hears, then dream on it.
 *
 * The plugin the composer_receive entry point was designed around: it
 * exports BOTH halves of the interface. receive() trains -- every note
 * arriving from the upstream stage bumps a transition count from the
 * previous pitch (order 1) or the previous pair (order 2) -- and tick()
 * emits a walk through the learned table, weighted by what was heard
 * most.
 *
 * Today the teacher is whatever generator sits upstream in the chain:
 * put a markov stage after an lsystem and it studies the grammar's
 * melody while emitting its own paraphrase of it. When live MIDI input
 * reaches chains (the `input midi' plumbing is parsed but not yet
 * wired), the same receive() trains on playing, unchanged -- that is
 * the point of the entry point.
 *
 * `pass' says whether what is heard is also passed downstream: 1 and
 * the teacher sounds alongside the dream, 0 and the teacher is silent
 * -- a generator you hear only through the student's ears.
 *
 * What is learned does not survive the instance: reset() wipes the
 * table, which is the honest meaning of "replay the piece" for a
 * learner (COMPOSITION_HANDOFF.md §7; composer_serialize is the future
 * answer for a trained voice worth keeping). Determinism holds because
 * the teacher is deterministic and the sampling draws from the
 * instance seed.
 */

#include <algorithm>
#include <cstring>
#include <map>
#include <random>
#include <utility>
#include <vector>

#include <cairo.h>

#include "thcomposer.h"

enum { P_ORDER, P_PASS, P_PERIOD, P_HOLD, P_VEL, P_COUNT };

static int paramIndex[P_COUNT];

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "order",  "how many previous notes the next depends on",
          THC_PARAM_INT, 1, 2, 1, NULL, NULL },
        { "pass",   "1: what is heard also flows downstream",
          THC_PARAM_INT, 0, 1, 1, NULL, NULL },
        { "period", "time between dreamed notes", THC_PARAM_FLOAT,
          0.02, 60, 0.5, NULL, "s" },
        { "hold",   "time before note-off", THC_PARAM_FLOAT,
          0.01, 60, 0.4, NULL, "s" },
        { "vel",    "velocity of dreamed notes", THC_PARAM_INT,
          1, 127, 76, NULL, NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_GENERATOR | THC_TRANSFORMER);
    info->set_desc(info->host,
        "Learns transitions from what it hears; emits the walk.");

    return 0;
}

/* A state is (prev, prevPrev); order 1 pins prevPrev to -1 so both
 * orders live in one table. -1 alone is the start state. */
typedef std::pair<int, int> MarkovState;

struct State {
    const thcParams *params;
    std::mt19937     rng;

    std::map<MarkovState, std::map<int, int> > table;
    int trained;                 /* transitions learned, for the draw   */

    int hearPrev, hearPrevPrev;  /* training context                    */
    int emitPrev, emitPrevPrev;  /* walking context                     */

    int  orderNow (void) const
    {
        return (int)params->get(params->ctx, paramIndex[P_ORDER]) >= 2
            ? 2 : 1;
    }

    MarkovState key (int prev, int prevPrev) const
    {
        return MarkovState(prev, orderNow() == 2 ? prevPrev : -1);
    }
};

extern "C" THINK_PLUGIN_API void *
composer_create (const thcParams *params)
{
    State *st = new State;

    st->params = params;
    st->rng.seed(params->seed);
    st->trained = 0;
    st->hearPrev = st->hearPrevPrev = -1;
    st->emitPrev = st->emitPrevPrev = -1;

    return st;
}

extern "C" THINK_PLUGIN_API void
composer_receive (void *state, const thcEvent *ev, thcEventSink *out)
{
    State *st = static_cast<State *>(state);
    const thcParams *p = st->params;

    if (ev->type == THC_EV_NOTE)
    {
        int note = ev->u.note.note;

        st->table[st->key(st->hearPrev, st->hearPrevPrev)][note]++;
        st->trained++;

        st->hearPrevPrev = st->hearPrev;
        st->hearPrev = note;
    }

    if ((int)p->get(p->ctx, paramIndex[P_PASS]) != 0)
        out->emit(out->ctx, ev);
}

extern "C" THINK_PLUGIN_API double
composer_tick (void *state, const thcTransport *t, thcEventSink *out)
{
    State *st = static_cast<State *>(state);
    const thcParams *p = st->params;
    auto get = [&](int i) { return p->get(p->ctx, paramIndex[i]); };

    if (t->running && !st->table.empty())
    {
        /* The learned row for where the walk stands; an unseen state
           falls back to a uniformly random known row rather than
           silence -- the walk rejoins the material it knows. */
        std::map<MarkovState, std::map<int, int> >::iterator row =
            st->table.find(st->key(st->emitPrev, st->emitPrevPrev));

        if (row == st->table.end())
        {
            size_t skip = st->rng() % st->table.size();

            row = st->table.begin();
            std::advance(row, skip);
        }

        int total = 0;

        for (std::map<int, int>::iterator i = row->second.begin();
             i != row->second.end(); ++i)
            total += i->second;

        if (total > 0)
        {
            int pick = (int)(st->rng() % total);
            int note = -1;

            for (std::map<int, int>::iterator i = row->second.begin();
                 i != row->second.end(); ++i)
            {
                pick -= i->second;

                if (pick < 0)
                {
                    note = i->first;
                    break;
                }
            }

            if (note >= 0)
            {
                thcEvent ev = {};

                ev.type = THC_EV_NOTE;
                ev.at = t->now;
                ev.channel = 0;          /* the sink routes             */
                ev.u.note.note = note;
                ev.u.note.velocity = (int)get(P_VEL);
                ev.u.note.duration = get(P_HOLD);

                out->emit(out->ctx, &ev);

                st->emitPrevPrev = st->emitPrev;
                st->emitPrev = note;
            }
        }
    }

    return t->now + get(P_PERIOD);
}

/* The transition table as a heat grid: rows are "from", columns "to",
 * over the pitches seen so far (capped to keep cells legible). The
 * walk's current pitch gets a halo on its row. */
extern "C" THINK_PLUGIN_API void
composer_draw (void *state, cairo_t *cr, double w, double h)
{
    State *st = static_cast<State *>(state);

    /* Which pitches has it heard? (Capped at 12.) */
    std::vector<int> seen;

    for (std::map<MarkovState, std::map<int, int> >::iterator r =
             st->table.begin(); r != st->table.end(); ++r)
        for (std::map<int, int>::iterator c = r->second.begin();
             c != r->second.end(); ++c)
        {
            bool have = false;

            for (size_t i = 0; i < seen.size(); i++)
                if (seen[i] == c->first)
                    have = true;

            if (!have && seen.size() < 12)
                seen.push_back(c->first);
        }

    if (seen.empty())
        return;

    std::sort(seen.begin(), seen.end());

    double cell = (w < h ? w : h) / seen.size();
    double ox = (w - cell * seen.size()) / 2;
    double oy = (h - cell * seen.size()) / 2;

    int maxCount = 1;

    for (std::map<MarkovState, std::map<int, int> >::iterator r =
             st->table.begin(); r != st->table.end(); ++r)
        for (std::map<int, int>::iterator c = r->second.begin();
             c != r->second.end(); ++c)
            if (c->second > maxCount)
                maxCount = c->second;

    for (size_t fi = 0; fi < seen.size(); fi++)
        for (size_t ti = 0; ti < seen.size(); ti++)
        {
            /* Order-2 rows are folded onto the `prev' pitch for the
               picture; the true table keeps the pairs. */
            int count = 0;

            for (std::map<MarkovState, std::map<int, int> >::iterator r =
                     st->table.begin(); r != st->table.end(); ++r)
                if (r->first.first == seen[fi] &&
                    r->second.count(seen[ti]))
                    count += r->second[seen[ti]];

            if (count == 0)
                continue;

            cairo_set_source_rgba(cr, 1.0, 0.85, 0.3,
                                  0.15 + 0.75 * count / maxCount);
            cairo_rectangle(cr, ox + ti * cell + 0.5,
                            oy + fi * cell + 0.5, cell - 1, cell - 1);
            cairo_fill(cr);
        }

    for (size_t fi = 0; fi < seen.size(); fi++)
        if (seen[fi] == st->emitPrev)
        {
            cairo_set_source_rgba(cr, 1, 1, 1, 0.7);
            cairo_set_line_width(cr, 1);
            cairo_rectangle(cr, ox + 0.5, oy + fi * cell + 0.5,
                            cell * seen.size() - 1, cell - 1);
            cairo_stroke(cr);
        }
}

extern "C" THINK_PLUGIN_API void
composer_destroy (void *state)
{
    delete static_cast<State *>(state);
}
