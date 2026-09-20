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

/* euclid -- a Euclidean rhythm: `fills' onsets spread as evenly as
 * possible over `steps' steps (Bjorklund's algorithm; the arithmetic
 * form below is equivalent and needs no lists). E(3,8) is the tresillo,
 * E(5,8) the cinquillo -- most of the world's ostinatos fall out of two
 * integers.
 *
 * `period' is the length of ONE step, and the unit decides the clock as
 * always: `period = 0.25 beats' is a sixteenth-note grid that follows
 * tempo automation; `period = 0.18 s' free-runs against everything else,
 * which in this framework is a feature.
 *
 * This is also the first plugin with a composer_draw: the ring every
 * Euclidean sequencer draws, because it is the honest picture of the
 * pattern. It reads instance state directly -- everything is the GUI
 * thread; that is the whole tier-two visualizer story.
 */

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <random>

#ifndef THC_NO_DRAW
#include <cairo.h>
#endif

#include "thcomposer.h"

/* M_PI is not in C++ and UCRT hides it; thMath.h is the one place that
 * knows that. See its header for why there are two answers and not one. */
#include "thMath.h"

/* THE POOL IS A SEQUENCER. With `fills' equal to `steps' every step is
 * an onset and the pool is simply played in order, one note per step,
 * wrapping when it runs out -- so a pool of a hundred and twenty-eight
 * sixteenths is eight bars of arpeggio, and a pool of five notes under
 * each chord of a four-bar progression is a bass line (orrery.gen). A
 * `.' in the pool is a rest: it takes its onset and sounds nothing, so
 * the pool carries the rhythm too, and a ring with every step filled
 * plus a pool with rests in it is a step sequencer with no other name.
 * The rest resolves as -1 at the file boundary, like every pitch; the
 * only thing this plugin knows is that a note below zero is not
 * played.
 *
 * AND EVERY `every'-TH CYCLE IS A FILL. A second pool, `fill', and the
 * cycle count: with `every = 4' the fourth time round the ring takes its
 * pitches from `fill' instead of `notes', which is a drum fill -- or a
 * turnaround, or the bar of the riff that answers the other three --
 * written in the stage that plays it rather than as a second chain under
 * a gate. `every = 0', the default, never fills.
 *
 * A FILL IS AN OVERLAY. The main pool goes on turning through a fill
 * cycle exactly as though it had played it, so every cycle that is not a
 * fill plays the note it would have played had there been no fill at
 * all. That is what makes `every' safe to add to a piece that is already
 * written: a pool of a hundred and twenty-eight notes laid over an
 * eight-bar progression stays with the chords, where a pool that stood
 * still through each fill would walk away from them a bar at a time. The
 * fill pool keeps its own place, and moves only on a fill, so a pool of
 * two bars' worth alternates between two fills rather than repeating
 * one. */

enum { P_STEPS, P_FILLS, P_ROTATE, P_NOTES, P_FILL, P_EVERY, P_VEL,
       P_HOLD, P_PERIOD, P_AHEAD, P_COUNT };

static int paramIndex[P_COUNT];

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "steps",  "steps in the cycle", THC_PARAM_INT,
          1, 64, 16, NULL, NULL },
        { "fills",  "onsets spread across the steps", THC_PARAM_INT,
          0, 64, 4, NULL, NULL },
        { "rotate", "rotate the pattern this many steps", THC_PARAM_INT,
          0, 63, 0, NULL, NULL },
        { "notes",  "pitch pool, cycled through the onsets",
          THC_PARAM_NOTESET, 0, 0, 0, "60", NULL },
        { "fill",   "pitch pool for the fill cycles",
          THC_PARAM_NOTESET, 0, 0, 0, "60", NULL },
        { "every",  "play the fill pool every nth cycle; 0 never",
          THC_PARAM_INT, 0, 64, 0, NULL, NULL },
        { "vel",    "velocity", THC_PARAM_INT, 1, 127, 96, NULL, NULL },
        { "hold",   "time before note-off", THC_PARAM_FLOAT,
          0.01, 60, 0.25, NULL, "s" },
        { "period", "length of one step", THC_PARAM_FLOAT,
          0.02, 60, 0.25, NULL, "s" },
        { "ahead", "emit a cycle at its start; knob and chanarg values are "
          "read once per cycle", THC_PARAM_INT,
          0, 1, 0, NULL, NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_GENERATOR | THC_EMITS_AHEAD);
    info->set_desc(info->host,
        "A Euclidean rhythm: fills onsets over steps steps.");

    return 0;
}

struct State {
    const thcParams *params;
    int              pos;       /* current step, for tick and for draw   */
    int              onsetNum;  /* which onset we are at, cycles the pool*/
    int              pool[128];
    int              poolLen;

    /* The fill pool and its own place in the queue, and which time round
       the ring this is -- counted from the first, so `every = 4' fills
       on cycles 3, 7, 11 and the first three bars are the riff. */
    int              fill[128];
    int              fillLen;
    int              fillNum;
    int              cycle;

    void reparseNotes (void);
    void reparseFill (void);

    /* Is this cycle a fill? Asked by tick and by the draw, so the ring
       and the sound cannot disagree about it. */
    bool filling (void) const;
};

/* One pool, from a resolved list of MIDI numbers. */
static int
parsePool (const char *s, int *out)
{
    int len = 0;

    while (s && *s && len < 128)
    {
        int n = atoi(s);

        if ((n >= 0 && n <= 127) || n == -1)   /* -1 is a rest        */
            out[len++] = n;

        if ((s = strchr(s, ',')))
            s++;
    }

    return len;
}

void
State::reparseNotes (void)
{
    poolLen = parsePool(params->get_string(params->ctx,
                                           paramIndex[P_NOTES]), pool);
}

void
State::reparseFill (void)
{
    fillLen = parsePool(params->get_string(params->ctx,
                                           paramIndex[P_FILL]), fill);
}

bool
State::filling (void) const
{
    const int every = (int)params->get(params->ctx, paramIndex[P_EVERY]);

    return every > 0 && fillLen > 0 && (cycle + 1) % every == 0;
}

/* Whether step i of E(fills, steps) carries an onset. This is the
 * standard arithmetic characterization: the onsets are the steps where
 * the running total of fills/steps crosses an integer. */
static bool
onsetAt (int i, int steps, int fills, int rotate)
{
    if (steps <= 0 || fills <= 0)
        return false;

    if (fills >= steps)
        return true;

    int k = ((i + rotate) % steps + steps) % steps;

    return (k * fills) % steps < fills;
}

extern "C" THINK_PLUGIN_API void *
composer_create (const thcParams *params)
{
    State *st = new State;

    st->params = params;
    st->pos = 0;
    st->onsetNum = 0;
    st->fillNum = 0;
    st->cycle = 0;
    st->reparseNotes();
    st->reparseFill();

    return st;
}

extern "C" THINK_PLUGIN_API void
composer_param_changed (void *state, int index)
{
    if (index == paramIndex[P_NOTES])
        static_cast<State *>(state)->reparseNotes();
    else if (index == paramIndex[P_FILL])
        static_cast<State *>(state)->reparseFill();
}

extern "C" THINK_PLUGIN_API double
composer_tick (void *state, const thcTransport *t, thcEventSink *out)
{
    State *st = static_cast<State *>(state);
    const thcParams *p = st->params;
    auto get = [&](int i) { return p->get(p->ctx, paramIndex[i]); };

    int steps = (int)get(P_STEPS);

    if (steps < 1)
        steps = 1;

    if (st->pos >= steps)
        st->pos = 0;

    if ((int)get(P_AHEAD) != 0)
    {
        /* A change to ahead during a stepwise cycle sends only the steps
           still to come. All of their params are sampled at this wake. */
        const int remaining = steps - st->pos;
        const double rawPeriod = get(P_PERIOD);
        const double period = std::isfinite(rawPeriod) && rawPeriod > 0
            ? rawPeriod : 0.001;
        const int fills = (int)get(P_FILLS);
        const int rotate = (int)get(P_ROTATE);
        const int velocity = (int)get(P_VEL);
        const double hold = get(P_HOLD);
        const bool onFill = st->filling();

        for (int i = 0; i < remaining; i++)
        {
            if (!t->running || (onFill ? st->fillLen : st->poolLen) == 0 ||
                !onsetAt(st->pos + i, steps, fills, rotate))
                continue;

            const int note = onFill ? st->fill[st->fillNum % st->fillLen]
                                    : st->pool[st->onsetNum % st->poolLen];

            st->onsetNum++;

            if (onFill)
                st->fillNum++;

            if (note < 0)
                continue;

            thcEvent ev = {};

            ev.type = THC_EV_NOTE;
            ev.at = t->now + i * period;
            ev.channel = 0;
            ev.u.note.note = note;
            ev.u.note.velocity = velocity;
            ev.u.note.duration = hold;

            out->emit(out->ctx, &ev);
        }

        st->pos = 0;
        st->cycle++;

        return t->now + remaining * period;
    }

    const bool onFill = st->filling();

    if (t->running && (onFill ? st->fillLen : st->poolLen) > 0 &&
        onsetAt(st->pos, steps, (int)get(P_FILLS), (int)get(P_ROTATE)))
    {
        const int note = onFill ? st->fill[st->fillNum % st->fillLen]
                                : st->pool[st->onsetNum % st->poolLen];

        /* Both queues move, and a rest takes its turn in each: the main
           one because a fill is an overlay and not an interruption (see
           the top of this file), the fill's own only while it plays. */
        st->onsetNum++;

        if (onFill)
            st->fillNum++;

        if (note >= 0)
        {
            thcEvent ev = {};

            ev.type = THC_EV_NOTE;
            ev.at = t->now;
            ev.channel = 0;                /* the sink routes            */
            ev.u.note.note = note;
            ev.u.note.velocity = (int)get(P_VEL);
            ev.u.note.duration = get(P_HOLD);

            out->emit(out->ctx, &ev);
        }
    }

    st->pos = (st->pos + 1) % steps;

    if (st->pos == 0)
        st->cycle++;

    return t->now + get(P_PERIOD);
}

/* The ring: one dot per step, filled where an onset falls, the current
 * step haloed. Step 0 at twelve o'clock, time running clockwise. */
#ifndef THC_NO_DRAW
extern "C" THINK_PLUGIN_API void
composer_draw (void *state, cairo_t *cr, double w, double h)
{
    State *st = static_cast<State *>(state);
    const thcParams *p = st->params;
    auto get = [&](int i) { return p->get(p->ctx, paramIndex[i]); };

    int steps = (int)get(P_STEPS);
    int fills = (int)get(P_FILLS);
    int rotate = (int)get(P_ROTATE);

    if (steps < 1)
        steps = 1;

    double cx = w / 2, cy = h / 2;
    double radius = (w < h ? w : h) / 2 - 8;

    if (radius < 4)
        return;

    cairo_set_source_rgba(cr, 1, 1, 1, 0.15);
    cairo_set_line_width(cr, 1);
    cairo_arc(cr, cx, cy, radius, 0, 2 * M_PI);
    cairo_stroke(cr);

    double dot = radius / 6;

    if (dot < 2) dot = 2;
    if (dot > 5) dot = 5;

    for (int i = 0; i < steps; i++)
    {
        double a = 2 * M_PI * i / steps - M_PI / 2;
        double x = cx + radius * cos(a);
        double y = cy + radius * sin(a);

        if (onsetAt(i, steps, fills, rotate))
        {
            /* A fill cycle is a different ring: the onsets are the same
               onsets and the notes on them are not, and the picture
               should not claim otherwise. */
            if (st->filling())
                cairo_set_source_rgba(cr, 0.45, 0.8, 1.0, 0.9);
            else
                cairo_set_source_rgba(cr, 1.0, 0.85, 0.3, 0.9);
            cairo_arc(cr, x, y, dot, 0, 2 * M_PI);
            cairo_fill(cr);
        }
        else
        {
            cairo_set_source_rgba(cr, 1, 1, 1, 0.35);
            cairo_arc(cr, x, y, dot * 0.6, 0, 2 * M_PI);
            cairo_stroke(cr);
        }

        /* The step about to fire wears the halo: pos has already been
           advanced past the step that just sounded. */
        if (i == st->pos)
        {
            cairo_set_source_rgba(cr, 1, 1, 1, 0.8);
            cairo_arc(cr, x, y, dot + 2.5, 0, 2 * M_PI);
            cairo_stroke(cr);
        }
    }
}
#endif

extern "C" THINK_PLUGIN_API void
composer_destroy (void *state)
{
    delete static_cast<State *>(state);
}
