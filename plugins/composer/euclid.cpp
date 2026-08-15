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

#include <cairo.h>

#include "thcomposer.h"

enum { P_STEPS, P_FILLS, P_ROTATE, P_NOTES, P_VEL, P_HOLD, P_PERIOD,
       P_COUNT };

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
        { "vel",    "velocity", THC_PARAM_INT, 1, 127, 96, NULL, NULL },
        { "hold",   "time before note-off", THC_PARAM_FLOAT,
          0.01, 60, 0.25, NULL, "s" },
        { "period", "length of one step", THC_PARAM_FLOAT,
          0.02, 60, 0.25, NULL, "s" },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_GENERATOR);
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

    void reparseNotes (void);
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
    st->reparseNotes();

    return st;
}

extern "C" THINK_PLUGIN_API void
composer_param_changed (void *state, int index)
{
    if (index == paramIndex[P_NOTES])
        static_cast<State *>(state)->reparseNotes();
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

    if (t->running && st->poolLen &&
        onsetAt(st->pos, steps, (int)get(P_FILLS), (int)get(P_ROTATE)))
    {
        thcEvent ev = {};

        ev.type = THC_EV_NOTE;
        ev.at = t->now;
        ev.channel = 0;                    /* the sink routes            */
        ev.u.note.note = st->pool[st->onsetNum % st->poolLen];
        ev.u.note.velocity = (int)get(P_VEL);
        ev.u.note.duration = get(P_HOLD);

        out->emit(out->ctx, &ev);
        st->onsetNum++;
    }

    st->pos = (st->pos + 1) % steps;

    return t->now + get(P_PERIOD);
}

/* The ring: one dot per step, filled where an onset falls, the current
 * step haloed. Step 0 at twelve o'clock, time running clockwise. */
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

extern "C" THINK_PLUGIN_API void
composer_destroy (void *state)
{
    delete static_cast<State *>(state);
}
