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

/* breed -- a genetic algorithm over timbres.
 *
 * `evolve' does this to phrases. A patch's declared chanargs are a vector
 * of floats and a vector of floats is a genome, so everything that plugin
 * does applies here verbatim: a population, tournament selection,
 * single-point crossover, per-gene mutation, elites carried unchanged,
 * and the current champion *played* every cycle -- emitted as
 * THC_EV_CHANARG events so what you hear is the search. This is tier 2 of
 * COMPOSITION_HANDOFF.md §9 and the autonomous shape of §7: fitness the
 * plugin computes.
 *
 * The interesting difference from evolve is where the genome's bounds
 * come from. A phrase's genes are degrees on a pitch ladder the piece
 * hands over; a timbre's genes are *knobs on somebody's instrument*, and
 * a search free to drive them anywhere would be reaching past what the
 * patch declared. So the bounds are the piece's own presets: each
 * component may travel between the values `from' and `toward' give it,
 * widened by `spread'. Naming one preset gives a neighbourhood around it;
 * naming two gives the corridor between them. Nothing here can invent a
 * component neither preset mentions.
 *
 * That is §9's first principle -- the declared surface is consent --
 * arriving as arithmetic rather than as a rule someone has to remember.
 *
 * The fitness function is taste with a number on it, and says so:
 *
 *   aim      closeness to `toward', if a target was named. The piece
 *            saying where it would like the instrument to end up.
 *   drift    distance from the champion just played, rewarded. The term
 *            that keeps the search alive: without it a GA finds a local
 *            optimum in a minute and, with elites protecting it, holds
 *            the same timbre forever. evolve calls this one `boredom'
 *            and the argument is the same one -- the ear is part of the
 *            landscape and the ear tires. Set it to zero on purpose and
 *            the instrument settles, which is also a choice.
 *   reach    a mild reward for using the corridor rather than huddling
 *            at one end of it, so a population does not collapse onto
 *            its seed and stay there.
 *
 * Audio-feature fitness -- judging the sound rather than the vector --
 * is the shadow-synth service at staging step 5, deliberately not here.
 * What this needs to become that is a different fitness() and nothing
 * else, which is most of the argument for building it this way first.
 *
 * All randomness from the per-instance seed, so a replay is exact.
 */

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include <cairo.h>

#include "thcomposer.h"
/* M_PI is not in C++ and UCRT hides it; thMath.h is the one place that
 * knows that. See its header for why there are two answers and not one. */
#include "thMath.h"

enum { P_FROM, P_TOWARD, P_POPULATION, P_MUTATION, P_ELITES, P_SPREAD,
       P_AIM, P_DRIFT, P_REACH, P_PERIOD, P_COUNT };

static int paramIndex[P_COUNT];

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "from",   "preset the population starts around", THC_PARAM_PRESET,
          0, 0, 0, "", NULL },
        { "toward", "preset the search aims at; omit for no target",
          THC_PARAM_PRESET, 0, 0, 0, "", NULL },
        { "population", "genomes alive at once", THC_PARAM_INT,
          4, 128, 16, NULL, NULL },
        { "mutation", "how far a mutated gene moves", THC_PARAM_FLOAT,
          0, 1, 0.12, NULL, NULL },
        { "elites", "champions carried into the next generation unchanged",
          THC_PARAM_INT, 0, 8, 2, NULL, NULL },
        { "spread", "how far outside the presets a gene may travel",
          THC_PARAM_FLOAT, 0, 1, 0.15, NULL, NULL },
        { "aim",    "weight on closeness to `toward'", THC_PARAM_FLOAT,
          0, 4, 1, NULL, NULL },
        { "drift",  "weight on being unlike the timbre just played",
          THC_PARAM_FLOAT, 0, 4, 0.6, NULL, NULL },
        { "reach",  "weight on using the whole corridor", THC_PARAM_FLOAT,
          0, 4, 0.25, NULL, NULL },
        { "period", "time between generations", THC_PARAM_FLOAT,
          0.05, 600, 6, NULL, "s" },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_GENERATOR);
    info->set_desc(info->host,
        "Breeds a population of chanarg vectors and plays the champion.");

    return 0;
}

/* ---- presets ----------------------------------------------------------- */

/* "res=0.8,fmin=0.06" -- the resolved form the loader hands over. Same
 * parser morph uses, and for the same reason: no plugin ever resolves a
 * preset, only reads one. */
static void
parsePreset (const char *text,
             std::vector<std::pair<std::string, double> > &out)
{
    out.clear();

    if (text == NULL)
        return;

    const char *p = text;

    while (*p)
    {
        const char *eq = strchr(p, '=');

        if (eq == NULL)
            break;

        std::string name(p, (size_t)(eq - p));

        char        *end = NULL;
        const double v = strtod(eq + 1, &end);

        if (end == eq + 1)
            break;

        out.push_back(std::make_pair(name, v));

        p = (*end == ',') ? end + 1 : end;

        while (*p == ',' || *p == ' ')
            p++;
    }
}

/* ---- instance ---------------------------------------------------------- */

/* One component of the genome: what it is called and how far it may go. */
struct Gene { std::string name; double lo, hi; };

typedef std::vector<double> Genome;      /* one value per gene           */

struct State {
    const thcParams *params;
    std::mt19937     rng;

    std::vector<Gene>   genes;
    std::vector<Genome> pop;
    Genome              champion;
    Genome              lastPlayed;

    std::string fromText, towardText, spreadKey;
    bool        seeded;
    int         generation;

    /* Drawn: the best fitness of each generation, so the search's shape
       is visible rather than only audible. */
    std::vector<double> fitHistory;
};

extern "C" THINK_PLUGIN_API void *
composer_create (const thcParams *params)
{
    State *st = new State;

    st->params = params;
    st->rng.seed(params->seed);
    st->seeded = false;
    st->generation = 0;

    return st;
}

extern "C" THINK_PLUGIN_API void
composer_destroy (void *state)
{
    delete static_cast<State *>(state);
}

static double
getp (State *st, int i)
{
    return st->params->get(st->params->ctx, paramIndex[i]);
}

/* The corridor each component may travel in.
 *
 * A component both presets name gets the interval between them; one only
 * `from' names gets a point, which `spread' then opens into a
 * neighbourhood. A component only `toward' names is still a legal
 * destination -- the target is allowed to reach for something the start
 * did not have -- and gets the same treatment from the other end.
 *
 * `spread' is a fraction of the corridor's own width, plus a floor, so a
 * pair of presets that are close together does not produce a search with
 * nowhere to go. */
static void
buildGenes (State *st)
{
    const char *f = st->params->get_string(st->params->ctx,
                                           paramIndex[P_FROM]);
    const char *t = st->params->get_string(st->params->ctx,
                                           paramIndex[P_TOWARD]);

    const std::string fs = f ? f : "";
    const std::string ts = t ? t : "";

    char key[32];

    snprintf(key, sizeof(key), "%.6f", getp(st, P_SPREAD));

    /* Rebuilt only when something it depends on changed. The comparison
       is on the values rather than on a dirty flag, so this is right
       however the change arrived. */
    if (fs == st->fromText && ts == st->towardText &&
        key == st->spreadKey && !st->genes.empty())
        return;

    st->fromText = fs;
    st->towardText = ts;
    st->spreadKey = key;

    std::vector<std::pair<std::string, double> > from, toward;

    parsePreset(fs.c_str(), from);
    parsePreset(ts.c_str(), toward);

    st->genes.clear();

    for (size_t i = 0; i < from.size(); i++)
    {
        Gene g;

        g.name = from[i].first;
        g.lo = g.hi = from[i].second;

        for (size_t j = 0; j < toward.size(); j++)
            if (toward[j].first == g.name)
            {
                g.lo = std::min(g.lo, toward[j].second);
                g.hi = std::max(g.hi, toward[j].second);
            }

        st->genes.push_back(g);
    }

    for (size_t j = 0; j < toward.size(); j++)
    {
        bool seen = false;

        for (size_t i = 0; i < from.size(); i++)
            if (from[i].first == toward[j].first)
                seen = true;

        if (seen)
            continue;

        Gene g;

        g.name = toward[j].first;
        g.lo = g.hi = toward[j].second;

        st->genes.push_back(g);
    }

    const double spread = getp(st, P_SPREAD);

    for (size_t i = 0; i < st->genes.size(); i++)
    {
        Gene &g = st->genes[i];
        const double pad = spread * (g.hi - g.lo) + spread * 0.1;

        g.lo -= pad;
        g.hi += pad;
    }

    /* The corridor moved, so the population living in it is stale. */
    st->pop.clear();
    st->seeded = false;
}

/* Where `toward' puts each gene, for the aim term. A gene the target does
 * not name has no opinion attached to it and contributes nothing. */
static void
targetOf (State *st, std::vector<double> &out, std::vector<char> &has)
{
    const char *t = st->params->get_string(st->params->ctx,
                                           paramIndex[P_TOWARD]);

    std::vector<std::pair<std::string, double> > toward;

    parsePreset(t ? t : "", toward);

    out.assign(st->genes.size(), 0.0);
    has.assign(st->genes.size(), 0);

    for (size_t i = 0; i < st->genes.size(); i++)
        for (size_t j = 0; j < toward.size(); j++)
            if (toward[j].first == st->genes[i].name)
            {
                out[i] = toward[j].second;
                has[i] = 1;
            }
}

/* Normalized so every term is comparable whatever the chanargs' ranges
 * are: a filter cutoff between 0 and 1 and a detune between 0 and 400
 * must not weigh differently just because one patch chose bigger
 * numbers. */
static double
norm (const Gene &g, double a, double b)
{
    const double w = g.hi - g.lo;

    return w > 1e-9 ? fabs(a - b) / w : 0.0;
}

static double
fitness (State *st, const Genome &g,
         const std::vector<double> &target, const std::vector<char> &hasTarget)
{
    const double aim = getp(st, P_AIM);
    const double drift = getp(st, P_DRIFT);
    const double reach = getp(st, P_REACH);

    double score = 0;

    /* aim: closer to the target is better, so the distance is subtracted. */
    if (aim > 0)
    {
        double d = 0;
        int    n = 0;

        for (size_t i = 0; i < g.size(); i++)
            if (hasTarget[i])
            {
                d += norm(st->genes[i], g[i], target[i]);
                n++;
            }

        if (n)
            score -= aim * (d / n);
    }

    /* drift: unlike what was just played is better. */
    if (drift > 0 && st->lastPlayed.size() == g.size())
    {
        double d = 0;

        for (size_t i = 0; i < g.size(); i++)
            d += norm(st->genes[i], g[i], st->lastPlayed[i]);

        score += drift * (d / (double)g.size());
    }

    /* reach: distance from the middle of the corridor, mildly rewarded,
       so a population does not huddle at one end of the space the piece
       opened for it. */
    if (reach > 0)
    {
        double d = 0;

        for (size_t i = 0; i < g.size(); i++)
            d += norm(st->genes[i], g[i],
                      (st->genes[i].lo + st->genes[i].hi) / 2);

        score += reach * (d / (double)g.size());
    }

    return score;
}

static void
scatter (State *st)
{
    const int want = (int)getp(st, P_POPULATION);

    std::uniform_real_distribution<double> uni(0.0, 1.0);

    st->pop.clear();

    for (int i = 0; i < want; i++)
    {
        Genome g(st->genes.size());

        for (size_t k = 0; k < st->genes.size(); k++)
            g[k] = st->genes[k].lo +
                uni(st->rng) * (st->genes[k].hi - st->genes[k].lo);

        st->pop.push_back(g);
    }

    /* The first genome is the seed preset exactly, so a piece that names
       one hears where it started before hearing what became of it. */
    if (!st->pop.empty())
        for (size_t k = 0; k < st->genes.size(); k++)
            st->pop[0][k] = std::min(std::max(st->genes[k].lo,
                                              (st->genes[k].lo +
                                               st->genes[k].hi) / 2),
                                     st->genes[k].hi);

    st->champion = st->pop.empty() ? Genome() : st->pop[0];
    st->seeded = true;
    st->generation = 0;
    st->fitHistory.clear();
}

static const Genome &
tournament (State *st, const std::vector<double> &fit)
{
    std::uniform_int_distribution<size_t> pick(0, st->pop.size() - 1);

    size_t best = pick(st->rng);

    for (int i = 0; i < 2; i++)
    {
        const size_t c = pick(st->rng);

        if (fit[c] > fit[best])
            best = c;
    }

    return st->pop[best];
}

static void
generation (State *st)
{
    if (st->pop.size() < 2)
        return;

    std::vector<double> target;
    std::vector<char>   hasTarget;

    targetOf(st, target, hasTarget);

    std::vector<double> fit(st->pop.size());

    for (size_t i = 0; i < st->pop.size(); i++)
        fit[i] = fitness(st, st->pop[i], target, hasTarget);

    std::vector<size_t> order(st->pop.size());

    for (size_t i = 0; i < order.size(); i++)
        order[i] = i;

    std::sort(order.begin(), order.end(),
              [&fit](size_t a, size_t b) { return fit[a] > fit[b]; });

    st->champion = st->pop[order[0]];
    st->fitHistory.push_back(fit[order[0]]);

    if (st->fitHistory.size() > 128)
        st->fitHistory.erase(st->fitHistory.begin());

    /* Elites first, unchanged: a champion may be dethroned but never
       lost to an unlucky crossover. */
    const int elites = std::min((int)getp(st, P_ELITES),
                                (int)st->pop.size());

    std::vector<Genome> next;

    for (int i = 0; i < elites; i++)
        next.push_back(st->pop[order[i]]);

    const double mutation = getp(st, P_MUTATION);

    std::uniform_real_distribution<double> uni(0.0, 1.0);
    std::normal_distribution<double>       jog(0.0, 1.0);
    std::uniform_int_distribution<size_t>  cut(0, st->genes.size());

    while (next.size() < st->pop.size())
    {
        const Genome a = tournament(st, fit);
        const Genome b = tournament(st, fit);

        /* Single-point crossover, and the point is over the *components*
           -- a timbre's genes are named knobs, so a cut splits "these
           settings from one parent, those from the other", which is a
           thing a person could have done by hand. */
        const size_t at = cut(st->rng);

        Genome child(st->genes.size());

        for (size_t k = 0; k < st->genes.size(); k++)
            child[k] = (k < at) ? a[k] : b[k];

        for (size_t k = 0; k < st->genes.size(); k++)
        {
            if (uni(st->rng) > mutation)
                continue;

            const double w = st->genes[k].hi - st->genes[k].lo;

            child[k] += jog(st->rng) * mutation * (w > 1e-9 ? w : 0.1);

            /* Clamped, not wrapped: the corridor is what the piece's
               presets declared, and a gene outside it would be reaching
               past what the instrument was offered for. */
            child[k] = std::min(std::max(child[k], st->genes[k].lo),
                                st->genes[k].hi);
        }

        next.push_back(child);
    }

    st->pop.swap(next);
    st->generation++;
}

extern "C" THINK_PLUGIN_API double
composer_tick (void *state, const thcTransport *t, thcEventSink *out)
{
    State *st = static_cast<State *>(state);

    buildGenes(st);

    const double period = getp(st, P_PERIOD);

    if (st->genes.empty())
        return t->now + period;      /* no presets named yet             */

    if (!st->seeded)
        scatter(st);

    if (!t->running)
        return t->now + period;

    /* Played first, bred second: what is heard this cycle is the
       champion the last generation produced, and the search runs while
       it sounds. Same order evolve uses, and for the same reason -- the
       piece is the search rather than a report on one. */
    for (size_t i = 0; i < st->genes.size() && i < st->champion.size(); i++)
    {
        thcEvent ev = {};

        ev.type = THC_EV_CHANARG;
        ev.at = t->now;
        ev.channel = 0;                    /* the sink routes            */
        ev.u.chanarg.name = st->genes[i].name.c_str();
        ev.u.chanarg.value = (float)st->champion[i];

        out->emit(out->ctx, &ev);
    }

    st->lastPlayed = st->champion;

    generation(st);

    return t->now + period;
}

extern "C" THINK_PLUGIN_API void
composer_param_changed (void *state, int)
{
    buildGenes(static_cast<State *>(state));
}

/* ---- draw -------------------------------------------------------------- */

/* One lane per gene: the corridor as a track, the whole population as
 * faint marks along it, and the champion as a bright one. The population
 * converging -- the marks crowding towards a point and then, when drift
 * pulls, scattering again -- is the search, and it is the thing worth
 * being able to see without reading numbers. */
extern "C" THINK_PLUGIN_API void
composer_draw (void *state, cairo_t *cr, double w, double h)
{
    State *st = static_cast<State *>(state);

    buildGenes(st);

    const size_t n = st->genes.size();

    if (n == 0 || w <= 0 || h <= 0)
        return;

    const double pad = 4;
    const double lane = (h - 2 * pad) / (double)n;
    const double x0 = pad + 44;
    const double x1 = w - pad;

    if (x1 <= x0 || lane <= 2)
        return;

    cairo_set_line_width(cr, 1.0);
    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, lane > 14 ? 10 : 8);

    for (size_t i = 0; i < n; i++)
    {
        const Gene &g = st->genes[i];
        const double y = pad + lane * (i + 0.5);
        const double span = g.hi - g.lo;

        cairo_set_source_rgba(cr, 1, 1, 1, 0.45);
        cairo_move_to(cr, pad, y + 3);
        cairo_show_text(cr, g.name.c_str());

        cairo_set_source_rgba(cr, 1, 1, 1, 0.18);
        cairo_move_to(cr, x0, y);
        cairo_line_to(cr, x1, y);
        cairo_stroke(cr);

        cairo_set_source_rgba(cr, 1, 1, 1, 0.30);

        for (size_t k = 0; k < st->pop.size(); k++)
        {
            if (i >= st->pop[k].size())
                continue;

            const double f = span > 1e-9
                ? (st->pop[k][i] - g.lo) / span : 0.5;

            cairo_arc(cr, x0 + (x1 - x0) * f, y, 1.6, 0, 2 * M_PI);
            cairo_fill(cr);
        }

        if (i < st->champion.size())
        {
            const double f = span > 1e-9
                ? (st->champion[i] - g.lo) / span : 0.5;

            cairo_set_source_rgba(cr, 0.6, 0.9, 0.65, 0.95);
            cairo_arc(cr, x0 + (x1 - x0) * f, y, 3.0, 0, 2 * M_PI);
            cairo_fill(cr);
        }
    }
}
