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

/* evolve -- a genetic algorithm whose evolution you can hear.
 *
 * A population of phrase genomes -- `length' steps, each a rest or a
 * degree on the pitch ladder -- is evaluated against a small, frankly
 * opinionated fitness function, and each cycle the current champion is
 * *played*: emitted whole, as a scheduled block, so the piano roll
 * shows the phrase the algorithm just committed to. While it sounds,
 * one generation passes (tournament selection, single-point crossover,
 * per-gene mutation, two elites carried unchanged). The piece is the
 * search: early generations wander, later ones settle, and turning the
 * mutation knob up mid-piece audibly reintroduces doubt.
 *
 * The fitness function is taste with a number on it, and says so:
 *
 *   density    how full the phrase is, pulled toward the `density'
 *              param -- silence is a feature, not a failure
 *   smoothness stepwise motion rewarded, leaps taxed, weighted by
 *              `smooth'
 *   cadence    a phrase that lands on the root outranks one that
 *              trails off; opening on the root is worth a little too
 *   monotony   three of the same note in a row starts to cost
 *   boredom    similarity to the phrase just played is taxed, weighted
 *              by `boredom' -- the term that keeps the search alive.
 *              Without it the algorithm does what GAs do: finds a
 *              local optimum inside a minute and, with elites
 *              protecting it and a static landscape offering nothing
 *              better, plays the same bars forever. The ear is part
 *              of the fitness landscape, and the ear tires; taxing
 *              yesterday's champion moves the optimum every cycle, so
 *              the piece circles a family of good phrases instead of
 *              freezing on one. Set it to zero to get the freeze back
 *              on purpose (an ostinato is a choice too).
 *
 * This is the autonomous shape from COMPOSITION_HANDOFF.md §7: fitness
 * the plugin computes. The interactive shape -- the user as fitness
 * function -- wants the composer_input ABI addition and is deliberately
 * not attempted here.
 *
 * DETERMINISM. Every random draw comes from the instance's seeded PRNG,
 * so the same piece converges down the same path every replay. reset()
 * rewinds evolution itself -- generation zero again, same wander.
 */

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <random>
#include <vector>

#include <cairo.h>

#include "thcomposer.h"

enum { P_NOTES, P_LENGTH, P_POP, P_MUTATE, P_DENSITY, P_SMOOTH,
       P_BOREDOM, P_STEP, P_HOLD, P_VEL, P_COUNT };

static int paramIndex[P_COUNT];

#define REST (-1)

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "notes",   "the pitch ladder genomes select from",
          THC_PARAM_NOTESET, 0, 0, 0, "48,50,52,55,57", NULL },
        { "length",  "steps per phrase", THC_PARAM_INT,
          4, 64, 16, NULL, NULL },
        { "population", "genomes in the population", THC_PARAM_INT,
          4, 64, 24, NULL, NULL },
        { "mutate",  "per-gene mutation probability", THC_PARAM_FLOAT,
          0, 1, 0.08, NULL, NULL },
        { "density", "target fraction of steps that sound",
          THC_PARAM_FLOAT, 0, 1, 0.6, NULL, NULL },
        { "smooth",  "how much leaps cost", THC_PARAM_FLOAT,
          0, 1, 0.5, NULL, NULL },
        { "boredom", "how quickly the last phrase wears out",
          THC_PARAM_FLOAT, 0, 1, 0.4, NULL, NULL },
        { "step",    "length of one step", THC_PARAM_FLOAT,
          0.02, 60, 0.25, NULL, "s" },
        { "hold",    "time before note-off", THC_PARAM_FLOAT,
          0.01, 60, 0.3, NULL, "s" },
        { "vel",     "velocity", THC_PARAM_INT, 1, 127, 84, NULL, NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_GENERATOR);
    info->set_desc(info->host,
        "A genetic algorithm; each cycle plays the current champion.");

    return 0;
}

typedef std::vector<int> Genome;         /* REST or a ladder degree      */

struct State {
    const thcParams *params;
    std::mt19937     rng;

    int  pool[128];
    int  poolLen;

    std::vector<Genome> pop;
    Genome champion;
    Genome lastPlayed;
    int    generation;

    std::deque<double> fitHistory;       /* best-of-generation, for draw */

    void reparseNotes (void);
    void reseedPopulation (void);
    double fitness (const Genome &g) const;
    Genome &tournament (void);
    void generationStep (void);
    int  degreeToMidi (int degree) const;
};

void
State::reparseNotes (void)
{
    const char *s = params->get_string(params->ctx, paramIndex[P_NOTES]);

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

int
State::degreeToMidi (int degree) const
{
    if (poolLen == 0 || degree < 0 || degree >= poolLen)
        return -1;

    return pool[degree];
}

void
State::reseedPopulation (void)
{
    int len = (int)params->get(params->ctx, paramIndex[P_LENGTH]);
    int n = (int)params->get(params->ctx, paramIndex[P_POP]);

    if (len < 1) len = 1;
    if (n < 2) n = 2;

    pop.assign(n, Genome());

    for (int i = 0; i < n; i++)
    {
        pop[i].resize(len);

        for (int j = 0; j < len; j++)
        {
            /* Rests seeded at roughly the target density's complement,
               so generation zero is already in the neighborhood. */
            double target =
                params->get(params->ctx, paramIndex[P_DENSITY]);

            if (std::uniform_real_distribution<double>(0, 1)(rng) < target
                && poolLen > 0)
                pop[i][j] = (int)(rng() % poolLen);
            else
                pop[i][j] = REST;
        }
    }

    champion = pop[0];
    lastPlayed.clear();
    generation = 0;
    fitHistory.clear();
}

/* Taste with a number on it -- see the header comment. Scores are only
 * compared with each other, so the scale is arbitrary. */
double
State::fitness (const Genome &g) const
{
    double target = params->get(params->ctx, paramIndex[P_DENSITY]);
    double smooth = params->get(params->ctx, paramIndex[P_SMOOTH]);

    int sounded = 0, leaps = 0, moves = 0, runs = 0;
    int prev = REST, prevPrev = REST;
    int first = REST, last = REST;

    for (size_t i = 0; i < g.size(); i++)
    {
        int v = g[i];

        if (v != REST)
        {
            sounded++;

            if (first == REST)
                first = v;
            last = v;

            if (prev != REST)
            {
                moves++;
                leaps += abs(v - prev) > 2 ? 1 : 0;
            }

            if (v == prev && prev == prevPrev)
                runs++;
        }

        prevPrev = prev;
        prev = v;
    }

    double f = 0;

    f -= fabs((double)sounded / g.size() - target) * 4.0;

    if (moves > 0)
        f -= smooth * 2.0 * leaps / moves;

    if (last == 0)
        f += 0.5;                        /* land on the root            */
    if (first == 0)
        f += 0.2;                        /* opening there helps too     */

    f -= 0.15 * runs;                    /* monotony tax                */

    /* The boredom tax: gene-for-gene similarity to what was just
       heard. This is what moves the optimum every cycle -- see the
       header. Scaled ahead of the other terms on purpose: at full
       weight, being yesterday's champion costs about as much as
       missing the density target entirely. */
    if (lastPlayed.size() == g.size() && !g.empty())
    {
        double boredom =
            params->get(params->ctx, paramIndex[P_BOREDOM]);
        int same = 0;

        for (size_t i = 0; i < g.size(); i++)
            if (g[i] == lastPlayed[i])
                same++;

        f -= boredom * 3.0 * same / g.size();
    }

    return f;
}

Genome &
State::tournament (void)
{
    size_t best = rng() % pop.size();

    for (int k = 1; k < 3; k++)
    {
        size_t c = rng() % pop.size();

        if (fitness(pop[c]) > fitness(pop[best]))
            best = c;
    }

    return pop[best];
}

void
State::generationStep (void)
{
    if (pop.size() < 2 || pop[0].empty())
        return;

    double mutate = params->get(params->ctx, paramIndex[P_MUTATE]);
    size_t len = pop[0].size();

    /* Elitism: the two best survive unchanged, so the champion can only
       be dethroned, never lost to a bad crossover. */
    std::vector<size_t> order(pop.size());

    for (size_t i = 0; i < order.size(); i++)
        order[i] = i;

    std::sort(order.begin(), order.end(),
              [this](size_t a, size_t b)
              { return fitness(pop[a]) > fitness(pop[b]); });

    std::vector<Genome> next;

    next.push_back(pop[order[0]]);
    next.push_back(pop[order[1]]);

    std::uniform_real_distribution<double> uni(0, 1);

    while (next.size() < pop.size())
    {
        const Genome &a = tournament();
        const Genome &b = tournament();
        size_t cut = 1 + rng() % (len > 1 ? len - 1 : 1);
        Genome child(len);

        for (size_t i = 0; i < len; i++)
            child[i] = i < cut ? a[i] : b[i];

        for (size_t i = 0; i < len; i++)
            if (uni(rng) < mutate)
                child[i] = poolLen > 0 && uni(rng) < 0.7
                    ? (int)(rng() % poolLen) : REST;

        next.push_back(child);
    }

    pop.swap(next);
    generation++;

    /* The champion the next tick will play. */
    size_t best = 0;

    for (size_t i = 1; i < pop.size(); i++)
        if (fitness(pop[i]) > fitness(pop[best]))
            best = i;

    champion = pop[best];

    fitHistory.push_back(fitness(champion));

    while (fitHistory.size() > 128)
        fitHistory.pop_front();
}

extern "C" THINK_PLUGIN_API void *
composer_create (const thcParams *params)
{
    State *st = new State;

    st->params = params;
    st->rng.seed(params->seed);
    st->reparseNotes();
    st->reseedPopulation();

    return st;
}

extern "C" THINK_PLUGIN_API void
composer_param_changed (void *state, int index)
{
    State *st = static_cast<State *>(state);

    if (index == paramIndex[P_NOTES])
    {
        st->reparseNotes();

        /* Degrees outside the new ladder become rests rather than
           crashes; evolution repopulates them soon enough. */
        for (size_t i = 0; i < st->pop.size(); i++)
            for (size_t j = 0; j < st->pop[i].size(); j++)
                if (st->pop[i][j] >= st->poolLen)
                    st->pop[i][j] = REST;
    }

    if (index == paramIndex[P_LENGTH] || index == paramIndex[P_POP])
        st->reseedPopulation();          /* a different search space is
                                            a different search          */
}

extern "C" THINK_PLUGIN_API double
composer_tick (void *state, const thcTransport *t, thcEventSink *out)
{
    State *st = static_cast<State *>(state);
    const thcParams *p = st->params;
    auto get = [&](int i) { return p->get(p->ctx, paramIndex[i]); };

    double step = get(P_STEP);

    if (step <= 0)
        step = 0.25;

    size_t len = st->champion.size();

    if (t->running && len > 0 && st->poolLen > 0)
    {
        /* Consecutive repeats tie into one longer note: a genome that
           evolved a held tone gets a held tone, not a stutter. */
        size_t i = 0;

        while (i < len)
        {
            int v = st->champion[i];

            if (v == REST)
            {
                i++;
                continue;
            }

            size_t run = 1;

            while (i + run < len && st->champion[i + run] == v)
                run++;

            int midi = st->degreeToMidi(v);

            if (midi >= 0)
            {
                thcEvent ev = {};

                ev.type = THC_EV_NOTE;
                ev.at = t->now + i * step;
                ev.channel = 0;          /* the sink routes             */
                ev.u.note.note = midi;
                ev.u.note.velocity = (int)get(P_VEL);
                ev.u.note.duration =
                    get(P_HOLD) + (run - 1) * step;

                out->emit(out->ctx, &ev);
            }

            i += run;
        }

        st->lastPlayed = st->champion;
        st->generationStep();
    }

    return t->now + (len > 0 ? len * step : step);
}

/* Left: the champion's contour, rests as gaps. Right margin: the last
 * 128 generations' best fitness, climbing as the search settles. */
extern "C" THINK_PLUGIN_API void
composer_draw (void *state, cairo_t *cr, double w, double h)
{
    State *st = static_cast<State *>(state);

    double mx = 6, my = 6;
    double iw = w - 2 * mx, ih = h - 2 * my;

    if (!st->champion.empty() && st->poolLen > 0)
    {
        double cellW = iw / st->champion.size();

        cairo_set_source_rgba(cr, 1.0, 0.85, 0.3, 0.85);

        for (size_t i = 0; i < st->champion.size(); i++)
        {
            int v = st->champion[i];

            if (v == REST)
                continue;

            double y = my + ih * (1.0 - (double)(v + 1) /
                                        (st->poolLen + 1));

            cairo_rectangle(cr, mx + i * cellW, y - 1.5,
                            cellW > 2 ? cellW - 1 : 1, 3);
            cairo_fill(cr);
        }
    }

    if (st->fitHistory.size() > 1)
    {
        double lo = st->fitHistory[0], hi = lo;

        for (size_t i = 1; i < st->fitHistory.size(); i++)
        {
            lo = std::min(lo, st->fitHistory[i]);
            hi = std::max(hi, st->fitHistory[i]);
        }

        if (hi - lo < 1e-9)
            hi = lo + 1e-9;

        cairo_set_source_rgba(cr, 1, 1, 1, 0.4);
        cairo_set_line_width(cr, 1);

        for (size_t i = 0; i < st->fitHistory.size(); i++)
        {
            double x = mx + iw * i / (st->fitHistory.size() - 1);
            double y = my + ih * (1.0 - (st->fitHistory[i] - lo) /
                                        (hi - lo));

            if (i == 0)
                cairo_move_to(cr, x, y);
            else
                cairo_line_to(cr, x, y);
        }

        cairo_stroke(cr);
    }
}

extern "C" THINK_PLUGIN_API void
composer_destroy (void *state)
{
    delete static_cast<State *>(state);
}
