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

#ifndef THC_NO_DRAW
#include <cairo.h>
#endif

#include "thcomposer.h"

/* The longest pattern the picture edits, and how many steps an empty one
   offers to be drawn into. */
#define MAX_PATTERN 64
#define EMPTY_STEPS 16

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

/* The pattern is drawn as a row of steps and can be clicked: a press
 * marks or clears the step under it, a drag paints what the press
 * painted, and the other button clears. What is drawn is held here until
 * the param says something new; composer_capture hands it back as the
 * param's text. */
struct State {
    const thcParams *params;

    bool touched;                   /* `drawn' is the pattern, not the param */
    char drawn[MAX_PATTERN + 1];
    char paintTo;                   /* what the drag paints                 */
    int  paintAt;                   /* the step the drag last painted       */
    int  lastStep;                  /* where the last note fell, for draw   */
};

/* The pattern in force: what was drawn, or the param's. */
static const char *
patternOf (const State *st)
{
    if (st->touched)
        return st->drawn;

    const char *p = st->params->get_string(st->params->ctx,
                                           paramIndex[P_PATTERN]);

    return p != NULL ? p : "";
}

extern "C" THINK_PLUGIN_API void *
composer_create (const thcParams *params)
{
    State *st = new State;

    st->params = params;
    st->touched = false;
    st->drawn[0] = 0;
    st->paintTo = 'x';
    st->paintAt = -1;
    st->lastStep = -1;

    return st;
}

extern "C" THINK_PLUGIN_API void
composer_param_changed (void *state, int index)
{
    if (index == paramIndex[P_PATTERN])
        static_cast<State *>(state)->touched = false;
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
    const char *pattern = patternOf(st);
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

    {
        const int len = (int)strlen(patternOf(st));
        const double grid = st->params->get(st->params->ctx,
                                            paramIndex[P_GRID]);

        if (len > 0 && grid > 0)
        {
            long n = (long)floor((ev->at < 0 ? 0 : ev->at) / grid + 1e-6);

            st->lastStep = (int)(n % len);
        }
    }
    const int i = (int)floor(v + 0.5);

    /* Quieter or louder, never absent: a stage that weights a line
       still plays every note of it. Dropping one is what xform::chance
       is for, and a velocity of nothing would be a note deleted by a
       multiplication. */
    copy.u.note.velocity = i < 1 ? 1 : i > 127 ? 127 : i;

    out->emit(out->ctx, &copy);
}

/* The pattern as a row of steps, the marked ones lit and the one the last
 * note fell on outlined; the swell, where there is one, as a line under
 * it from `from' to `to'. An empty pattern draws EMPTY_STEPS unmarked
 * steps, to be clicked into. */
#ifndef THC_NO_DRAW
extern "C" THINK_PLUGIN_API void
composer_draw (void *state, cairo_t *cr, double w, double h)
{
    State *st = static_cast<State *>(state);
    const char *pattern = patternOf(st);
    int len = (int)strlen(pattern);
    const bool empty = len == 0;

    if (empty)
        len = EMPTY_STEPS;

    if (len > MAX_PATTERN)
        len = MAX_PATTERN;

    const double cw = w / len;
    const double ch = h * 0.55 < cw * 3 ? h * 0.55 : cw * 3;
    const double top = (h - ch) / 2 - h * 0.08;

    for (int i = 0; i < len; i++)
    {
        const bool on = !empty && (pattern[i] == 'x' || pattern[i] == 'X');

        cairo_rectangle(cr, i * cw + 1, top, cw - 2, ch);

        if (on)
            cairo_set_source_rgba(cr, 1.0, 0.85, 0.3, 0.85);
        else
            cairo_set_source_rgba(cr, 1, 1, 1, 0.12);

        cairo_fill(cr);

        if (i == st->lastStep && !empty)
        {
            cairo_rectangle(cr, i * cw + 0.5, top - 0.5, cw - 1, ch + 1);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.8);
            cairo_set_line_width(cr, 1);
            cairo_stroke(cr);
        }
    }

    const thcParams *p = st->params;
    const double from = p->get(p->ctx, paramIndex[P_FROM]);
    const double to = p->get(p->ctx, paramIndex[P_TO]);

    if (from != to)
    {
        /* A multiplier of 1 a tenth of the way up from the foot. */
        const double base = h - 3, span = h * 0.1;

        cairo_set_source_rgba(cr, 0.45, 0.8, 1.0, 0.8);
        cairo_set_line_width(cr, 1.5);
        cairo_move_to(cr, 0, base - span * from);
        cairo_line_to(cr, w, base - span * to);
        cairo_stroke(cr);
    }
}
#endif

/* A press on a step marks it, or clears it where it was marked or with
 * the other button; a drag paints the same onto every step it crosses. */
extern "C" THINK_PLUGIN_API void
composer_input (void *state, const thcInputEvent *ev)
{
    State *st = static_cast<State *>(state);

    if (ev->type == THC_IN_RELEASE || ev->w <= 0)
        return;

    if (!st->touched)
    {
        const char *p = patternOf(st);
        size_t n = strlen(p);

        if (n > MAX_PATTERN)
            n = MAX_PATTERN;

        memcpy(st->drawn, p, n);
        st->drawn[n] = 0;
        st->touched = true;
    }

    int len = (int)strlen(st->drawn);

    if (len == 0)
    {
        memset(st->drawn, '.', EMPTY_STEPS);
        st->drawn[EMPTY_STEPS] = 0;
        len = EMPTY_STEPS;
    }

    const int i = (int)floor(ev->x / (ev->w / len));

    if (i < 0 || i >= len)
        return;

    const bool on = st->drawn[i] == 'x' || st->drawn[i] == 'X';

    if (ev->type == THC_IN_PRESS)
    {
        st->paintTo = ev->button == 3 || on ? '.' : 'x';
        st->paintAt = i;
    }

    /* Every step between the last one painted and this, since a quick
       drag reports far fewer positions than it crosses steps. */
    const int from = st->paintAt < 0 ? i : st->paintAt;
    const int a = from < i ? from : i, b = from < i ? i : from;

    for (int k = a; k <= b; k++)
        st->drawn[k] = st->paintTo;

    st->paintAt = i;
}

/* The drawn pattern, as the param's text. */
extern "C" THINK_PLUGIN_API const char *
composer_capture (void *state, int index)
{
    State *st = static_cast<State *>(state);

    return index == paramIndex[P_PATTERN] && st->touched ? st->drawn : NULL;
}
