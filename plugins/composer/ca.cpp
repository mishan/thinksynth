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

/* ca -- an elementary cellular automaton, one row per tick.
 *
 * A ring of `width' cells advances by one of Wolfram's 256 rules each
 * tick; cells map onto the pitch ladder (wrapping octaves past its
 * ends, like every ladder here). Rule 110 meanders, rule 90 makes
 * lace, rule 30 boils -- and because the rule is an ordinary param, it
 * can be automated by a knob and the texture reorganizes mid-piece.
 *
 * `scatter' picks the start: 0 is the classic single center cell
 * (fully deterministic, no seed involved); above 0 it is the initial
 * probability of life, drawn from the instance seed, so a scattered
 * start replays exactly too. `trigger' says what sounds: births only
 * (the default -- sustained regions read as texture, not as a chord
 * restruck every row) or every live cell.
 *
 * The composer_draw is the grid every CA wants: recent rows scrolling
 * upward, the present row at the bottom -- the piano roll's past in
 * cell form, and this plugin's whole state is on the canvas.
 *
 * AND IT IS CLICKABLE, which is not decoration. An empty ring is a fixed
 * point for every rule that maps 000 to 0, which is half of them --
 * including rule 0, which empties the ring on its first row. So turning
 * the rule knob down and back up does *not* bring the automaton back:
 * once the ring is all zeros the rule has nothing to work on, and the
 * stage is silent until the piece is reloaded. That is correct
 * automaton behaviour and a useless instrument.
 *
 * composer_input is the way out, and the right one: click a cell in the
 * canvas's enlarged view and it lives again. Nothing here re-seeds
 * itself behind your back, because a cellular automaton that quietly
 * repopulated would not be one. The draw says when the ring is empty so
 * the silence reads as a state rather than a hang.
 */

#include <cstdlib>
#include <cstring>
#include <deque>
#include <random>
#include <vector>

#include <cairo.h>

#include "thcomposer.h"

enum { P_RULE, P_WIDTH, P_SCATTER, P_TRIGGER, P_NOTES, P_PERIOD,
       P_HOLD, P_VEL, P_COUNT };

static int paramIndex[P_COUNT];

#define MAX_WIDTH 32
#define DRAW_ROWS 40

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "rule",    "Wolfram rule number", THC_PARAM_INT,
          0, 255, 110, NULL, NULL },
        { "width",   "cells in the ring", THC_PARAM_INT,
          3, 32, 16, NULL, NULL },
        { "scatter", "0: one center cell; else initial life probability",
          THC_PARAM_FLOAT, 0, 1, 0, NULL, NULL },
        { "trigger", "0: births only; 1: every live cell",
          THC_PARAM_INT, 0, 1, 0, NULL, NULL },
        { "notes",   "the pitch ladder cells land on", THC_PARAM_NOTESET,
          0, 0, 0, "48,50,52,55,57", NULL },
        { "period",  "length of one generation", THC_PARAM_FLOAT,
          0.02, 60, 0.25, NULL, "s" },
        { "hold",    "time before note-off", THC_PARAM_FLOAT,
          0.01, 60, 0.2, NULL, "s" },
        { "vel",     "velocity", THC_PARAM_INT, 1, 127, 84, NULL, NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_GENERATOR);
    info->set_desc(info->host,
        "An elementary cellular automaton, one row per tick.");

    return 0;
}

struct State {
    const thcParams *params;
    std::mt19937     rng;

    int  pool[128];
    int  poolLen;

    std::vector<char> cells;
    std::deque<std::vector<char> > history;   /* for the draw           */

    void reparseNotes (void);
    void reinit (void);
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
    if (poolLen == 0)
        return -1;

    int idx = degree % poolLen;
    int oct = degree / poolLen;
    int midi = pool[idx] + 12 * oct;

    return midi < 0 || midi > 127 ? -1 : midi;
}

void
State::reinit (void)
{
    int width = (int)params->get(params->ctx, paramIndex[P_WIDTH]);

    if (width < 3) width = 3;
    if (width > MAX_WIDTH) width = MAX_WIDTH;

    double scatter = params->get(params->ctx, paramIndex[P_SCATTER]);

    cells.assign(width, 0);
    history.clear();

    if (scatter <= 0)
        cells[width / 2] = 1;
    else
    {
        std::uniform_real_distribution<double> uni(0, 1);

        for (int i = 0; i < width; i++)
            cells[i] = uni(rng) < scatter ? 1 : 0;
    }
}

extern "C" THINK_PLUGIN_API void *
composer_create (const thcParams *params)
{
    State *st = new State;

    st->params = params;
    st->rng.seed(params->seed);
    st->reparseNotes();
    st->reinit();

    return st;
}

extern "C" THINK_PLUGIN_API void
composer_param_changed (void *state, int index)
{
    State *st = static_cast<State *>(state);

    if (index == paramIndex[P_NOTES])
        st->reparseNotes();

    /* A new width or start is a new automaton; a new rule is the same
       automaton changing its mind, which is the fun of automating it. */
    if (index == paramIndex[P_WIDTH] || index == paramIndex[P_SCATTER])
        st->reinit();
}

extern "C" THINK_PLUGIN_API double
composer_tick (void *state, const thcTransport *t, thcEventSink *out)
{
    State *st = static_cast<State *>(state);
    const thcParams *p = st->params;
    auto get = [&](int i) { return p->get(p->ctx, paramIndex[i]); };

    int width = (int)st->cells.size();

    if (t->running && width > 0 && st->poolLen > 0)
    {
        int rule = (int)get(P_RULE) & 0xff;
        bool births = (int)get(P_TRIGGER) == 0;

        /* Advance the ring. */
        std::vector<char> next(width);

        for (int i = 0; i < width; i++)
        {
            int l = st->cells[(i + width - 1) % width];
            int c = st->cells[i];
            int r = st->cells[(i + 1) % width];

            next[i] = (rule >> ((l << 2) | (c << 1) | r)) & 1;
        }

        /* Sound the row that just arrived. */
        for (int i = 0; i < width; i++)
        {
            if (!next[i])
                continue;

            if (births && st->cells[i])
                continue;

            int midi = st->degreeToMidi(i);

            if (midi < 0)
                continue;

            thcEvent ev = {};

            ev.type = THC_EV_NOTE;
            ev.at = t->now;
            ev.channel = 0;              /* the sink routes             */
            ev.u.note.note = midi;
            ev.u.note.velocity = (int)get(P_VEL);
            ev.u.note.duration = get(P_HOLD);

            out->emit(out->ctx, &ev);
        }

        st->history.push_back(st->cells);

        while (st->history.size() > DRAW_ROWS)
            st->history.pop_front();

        st->cells.swap(next);
    }

    return t->now + get(P_PERIOD);
}

/* Recent rows scrolling upward, the present at the bottom, live cells
 * golden. */
/* The present row is the bottom one, drawn across the full width, and a
 * click lands on whichever cell it is over. Only the present row is
 * touchable: the rows above it are history, and history is not a thing
 * you get to edit. */
extern "C" THINK_PLUGIN_API void
composer_input (void *state, const thcInputEvent *ev)
{
    State *st = static_cast<State *>(state);

    const int width = (int)st->cells.size();

    if (width == 0 || ev->w <= 0 || ev->h <= 0)
        return;

    if (ev->type == THC_IN_RELEASE)
        return;

    const size_t rows = st->history.size() + 1;
    const double ch = ev->h / (double)(rows > DRAW_ROWS ? rows : DRAW_ROWS);

    /* Anywhere in the bottom row's band. Being generous about the y is
       deliberate: the band is one row of a forty-row grid, and asking
       someone to hit four pixels to revive a dead automaton is asking
       them to reload instead. */
    if (ev->y < ev->h - ch * 2)
        return;

    const int i = (int)(ev->x / (ev->w / width));

    if (i < 0 || i >= width)
        return;

    /* The primary button toggles; any other clears, the same bargain
       gen::life makes, so a right-drag thins a row out. */
    st->cells[i] = (ev->button == 1) ? (st->cells[i] ? 0 : 1) : 0;
}

extern "C" THINK_PLUGIN_API void
composer_draw (void *state, cairo_t *cr, double w, double h)
{
    State *st = static_cast<State *>(state);
    int width = (int)st->cells.size();

    if (width == 0)
        return;

    size_t rows = st->history.size() + 1;
    double cw = w / width;
    double ch = h / (rows > DRAW_ROWS ? rows : DRAW_ROWS);
    double y = h - ch;

    /* An empty ring stays empty under every rule that maps 000 to 0, so
       this is not a pause, it is the end -- unless somebody clicks. Said
       plainly, because a silent stage that looks exactly like a working
       one is the difference between "I broke it" and "I see". */
    bool empty = true;

    for (int i = 0; i < width; i++)
        if (st->cells[i])
            empty = false;

    if (empty)
    {
        cairo_set_source_rgba(cr, 1, 1, 1, 0.45);
        cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, h > 60 ? 11 : 8);
        cairo_move_to(cr, 4, h / 2);
        cairo_show_text(cr, h > 60 ? "empty -- click a cell to seed it"
                                   : "empty");
    }

    /* The row a click lands in, marked so the target is visible rather
       than folklore. */
    cairo_set_source_rgba(cr, 1, 1, 1, 0.10);
    cairo_rectangle(cr, 0, y, w, ch);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, 1.0, 0.85, 0.3, 0.9);

    for (int i = 0; i < width; i++)
        if (st->cells[i])
        {
            cairo_rectangle(cr, i * cw, y, cw > 1.5 ? cw - 0.5 : cw, ch);
            cairo_fill(cr);
        }

    for (size_t r = 0; r < st->history.size(); r++)
    {
        const std::vector<char> &row =
            st->history[st->history.size() - 1 - r];
        double ry = y - (r + 1) * ch;
        double age = 0.7 * (1.0 - (double)r / DRAW_ROWS);

        if (ry + ch < 0)
            break;

        cairo_set_source_rgba(cr, 1.0, 0.85, 0.3, age);

        for (int i = 0; i < width && i < (int)row.size(); i++)
            if (row[i])
            {
                cairo_rectangle(cr, i * cw, ry,
                                cw > 1.5 ? cw - 0.5 : cw, ch);
                cairo_fill(cr);
            }
    }
}

extern "C" THINK_PLUGIN_API void
composer_destroy (void *state)
{
    delete static_cast<State *>(state);
}
