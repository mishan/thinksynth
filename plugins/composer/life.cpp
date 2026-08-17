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

/* life -- Conway's, played.
 *
 * A board on a torus advances one generation per tick under B3/S23, and
 * the cells that changed are heard: rows are degrees on the pitch ladder,
 * columns are time within the bar. `gen::ca' is the one-dimensional
 * version of this idea and the family resemblance is deliberate -- the
 * same ladder, the same `trigger' choice between births and every live
 * cell, the same claim that the draw is the whole of the plugin's state.
 *
 * What Life adds is that it is *worth touching*. Everything interesting
 * about it comes from what you put on the board, and the difference
 * between a glider and a block is one cell. So this is the first plugin
 * to export composer_input: click a cell in the canvas's enlarged view
 * and it toggles, drag to paint a row of them, and the next generation
 * takes it from there. That is COMPOSITION_HANDOFF.md §7's argument for
 * the entry point, arriving for exactly the case §7 named -- clicks on
 * the plugin's own draw area, which was draw-only.
 *
 * The board is an ordinary THC_PARAM_STRING, written the way Life
 * patterns have always been written: `.' dead, `O' alive, `/' ends a
 * row. So a piece can ship a glider, `composer_capture' can hand a
 * clicked board back as the same text, and the whole thing round-trips
 * through the file with no opaque blob and no ABI for saving state --
 * which is the question §7 filed under composer_serialize and this
 * plugin gets to duck, because a Life board already has a spelling.
 *
 * DETERMINISM. There is no randomness here at all unless `scatter' asks
 * for one, and that draws from the instance seed like everything else.
 * A board nobody clicked replays exactly; a board somebody clicked
 * replays given the same clicks. That is the boundary live MIDI already
 * has and it is the honest meaning of replaying a piece a person is
 * part of.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include <cairo.h>

#include "thcomposer.h"

enum { P_BOARD, P_WIDTH, P_HEIGHT, P_SCATTER, P_TRIGGER, P_WRAP,
       P_NOTES, P_PERIOD, P_HOLD, P_VEL, P_COUNT };

static int paramIndex[P_COUNT];

#define MAX_W 64
#define MAX_H 32

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        /* The default is a glider, because a glider is the shortest
           possible argument that this is worth listening to: five cells
           that walk across the board forever and sound a different
           chord every four generations. */
        { "board",   "the starting pattern: . dead, O alive, / ends a row",
          THC_PARAM_STRING, 0, 0, 0,
          ".O......../..O......./OOO......./........../........../"
          "........../........../..........", NULL },
        { "width",   "cells across", THC_PARAM_INT, 3, 64, 10, NULL, NULL },
        { "height",  "cells down; each row is a degree", THC_PARAM_INT,
          3, 32, 8, NULL, NULL },
        { "scatter", "0: use the board as written; else the chance a "
          "cell starts alive", THC_PARAM_FLOAT, 0, 1, 0, NULL, NULL },
        { "trigger", "0: births only; 1: every live cell",
          THC_PARAM_INT, 0, 1, 0, NULL, NULL },
        { "wrap",    "1: a torus; 0: cells off the edge are dead",
          THC_PARAM_INT, 0, 1, 1, NULL, NULL },
        { "notes",   "pitch ladder; row 0 is the bottom",
          THC_PARAM_NOTESET, 0, 0, 0, "48,50,52,55,57,60,62,64", NULL },
        { "period",  "time for one generation", THC_PARAM_FLOAT,
          0.02, 60, 0.5, NULL, "s" },
        { "hold",    "time before each note-off", THC_PARAM_FLOAT,
          0.01, 60, 0.45, NULL, "s" },
        { "vel",     "velocity", THC_PARAM_INT, 1, 127, 80, NULL, NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_GENERATOR);
    info->set_desc(info->host,
        "Conway's Game of Life; click the board to change what it plays.");

    return 0;
}

/* ---- the board --------------------------------------------------------- */

struct State {
    const thcParams *params;
    std::mt19937     rng;

    int w, h;
    std::vector<char> cells;     /* w * h, row 0 at the bottom          */
    std::vector<char> born;      /* what changed in the last generation */

    std::vector<int>  ladder;
    std::string       ladderText;

    std::string boardText;       /* what cells was built from           */
    std::string captured;        /* what composer_capture last returned */

    int  generation;
    bool started;

    /* Set by composer_input, cleared once tick() has seen it: a click
       means "this board, not the one the param still says", so the
       reload check below must not undo it. */
    bool touched;

    /* The last cell a drag painted and what it painted, so dragging
       across a row does not toggle the same cell forty times a second
       and so a drag paints one value rather than flickering. */
    int  paintX, paintY;
    char paintTo;
};

static double
getp (State *st, int i)
{
    return st->params->get(st->params->ctx, paramIndex[i]);
}

static int
idx (const State *st, int x, int y)
{
    return y * st->w + x;
}

/* "..O/OOO" -> cells. Anything that is not a row separator counts as a
 * cell, and anything that is not dead counts as alive, so `#' and `*'
 * and `1' all work -- Life patterns come from too many places to be
 * strict about which character means occupied. Short rows are padded
 * dead and long ones truncated, because the board's size is the params'
 * business and a pattern pasted from somewhere else should still land. */
static void
parseBoard (State *st, const std::string &text)
{
    st->cells.assign((size_t)st->w * st->h, 0);

    int x = 0, row = 0;

    for (size_t i = 0; i < text.size() && row < st->h; i++)
    {
        const char c = text[i];

        if (c == '/' || c == '\n')
        {
            row++;
            x = 0;
            continue;
        }

        if (c == ' ' || c == '\r')
            continue;

        if (x < st->w)
        {
            /* Rows are written top-down, the way every Life pattern is,
               and stored bottom-up, because row 0 being the lowest note
               is what makes the ladder read like a staff. */
            const int y = st->h - 1 - row;

            st->cells[idx(st, x, y)] = (c == '.' || c == '0') ? 0 : 1;
        }

        x++;
    }
}

static std::string
boardToString (const State *st)
{
    std::string out;

    for (int row = 0; row < st->h; row++)
    {
        const int y = st->h - 1 - row;

        if (row)
            out += '/';

        for (int x = 0; x < st->w; x++)
            out += st->cells[idx(st, x, y)] ? 'O' : '.';
    }

    return out;
}

static void
parseLadder (State *st, const char *text)
{
    st->ladder.clear();

    if (text == NULL)
        return;

    const char *p = text;

    while (*p)
    {
        char *end = NULL;
        const long v = strtol(p, &end, 10);

        if (end == p)
            break;

        st->ladder.push_back((int)v);
        p = (*end == ',') ? end + 1 : end;
    }

    if (st->ladder.empty())
        st->ladder.push_back(60);
}

/* Rebuilt when the size or the written board changed -- but never on top
 * of a board somebody clicked, which is the one thing here that has no
 * other copy. */
static void
refresh (State *st)
{
    const int w = (int)getp(st, P_WIDTH);
    const int h = (int)getp(st, P_HEIGHT);

    const char *board =
        st->params->get_string(st->params->ctx, paramIndex[P_BOARD]);
    const char *notes =
        st->params->get_string(st->params->ctx, paramIndex[P_NOTES]);

    const std::string boardText = board ? board : "";
    const std::string notesText = notes ? notes : "";

    if (notesText != st->ladderText || st->ladder.empty())
    {
        st->ladderText = notesText;
        parseLadder(st, notesText.c_str());
    }

    const bool resized = (w != st->w || h != st->h);

    if (!resized && boardText == st->boardText && !st->cells.empty())
        return;

    /* A board that was clicked, and a param that has since changed.
     *
     * Two different things arrive here and they want opposite answers.
     * The host writing back what *this plugin just handed it* -- a
     * Capture -- must not reparse: the text and the board already agree,
     * and rebuilding from it would be work at best and a rounding of the
     * author's spelling at worst. Anything else is somebody stating a
     * board in the file or the panel, and that wins: a person who types
     * a pattern after clicking one means the typed one, and a click that
     * outranked every later edit would be a board nobody could correct.
     *
     * Telling them apart is exact rather than heuristic, because the
     * plugin remembers the last text it gave out. */
    if (!resized && st->touched && boardText == st->captured)
    {
        st->boardText = boardText;
        return;
    }

    st->w = w;
    st->h = h;
    st->boardText = boardText;
    st->touched = false;

    const double scatter = getp(st, P_SCATTER);

    if (scatter > 0)
    {
        std::uniform_real_distribution<double> uni(0.0, 1.0);

        st->cells.assign((size_t)w * h, 0);

        for (size_t i = 0; i < st->cells.size(); i++)
            st->cells[i] = uni(st->rng) < scatter ? 1 : 0;
    }
    else
        parseBoard(st, boardText);

    st->born.assign((size_t)w * h, 0);
    st->generation = 0;
}

extern "C" THINK_PLUGIN_API void *
composer_create (const thcParams *params)
{
    State *st = new State;

    st->params = params;
    st->rng.seed(params->seed);
    st->w = st->h = 0;
    st->generation = 0;
    st->started = false;
    st->touched = false;
    st->paintX = st->paintY = -1;
    st->paintTo = 0;

    refresh(st);

    return st;
}

extern "C" THINK_PLUGIN_API void
composer_destroy (void *state)
{
    delete static_cast<State *>(state);
}

extern "C" THINK_PLUGIN_API void
composer_param_changed (void *state, int)
{
    refresh(static_cast<State *>(state));
}

/* ---- the rule ---------------------------------------------------------- */

static int
neighbours (const State *st, int x, int y, bool wrap)
{
    int n = 0;

    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
        {
            if (dx == 0 && dy == 0)
                continue;

            int nx = x + dx, ny = y + dy;

            if (wrap)
            {
                nx = (nx + st->w) % st->w;
                ny = (ny + st->h) % st->h;
            }
            else if (nx < 0 || ny < 0 || nx >= st->w || ny >= st->h)
                continue;

            n += st->cells[idx(st, nx, ny)] ? 1 : 0;
        }

    return n;
}

/* B3/S23, and not a param.
 *
 * `ca' makes its rule a knob because a Wolfram rule is one number and
 * all 256 of them are interesting. Life's rule is two sets, most
 * spellings of it are not worth hearing, and the thing this plugin is
 * for is the *board*. A rule knob here would be a second dial competing
 * with the one that matters. */
static void
step (State *st)
{
    const bool wrap = getp(st, P_WRAP) >= 0.5;

    std::vector<char> next((size_t)st->w * st->h, 0);

    st->born.assign((size_t)st->w * st->h, 0);

    for (int y = 0; y < st->h; y++)
        for (int x = 0; x < st->w; x++)
        {
            const int  n = neighbours(st, x, y, wrap);
            const bool alive = st->cells[idx(st, x, y)] != 0;
            const bool live = alive ? (n == 2 || n == 3) : (n == 3);

            next[idx(st, x, y)] = live ? 1 : 0;

            if (live && !alive)
                st->born[idx(st, x, y)] = 1;
        }

    st->cells.swap(next);
    st->generation++;
}

/* ---- playing it -------------------------------------------------------- */

extern "C" THINK_PLUGIN_API double
composer_tick (void *state, const thcTransport *t, thcEventSink *out)
{
    State *st = static_cast<State *>(state);

    refresh(st);

    const double period = getp(st, P_PERIOD);

    if (st->w <= 0 || st->h <= 0)
        return t->now + period;

    if (!t->running)
        return t->now + period;

    /* The first tick plays the board as it stands before advancing it,
       so a piece that ships a pattern is heard before it is changed. */
    if (st->started)
        step(st);
    else
    {
        st->started = true;
        st->born = st->cells;
    }

    const bool births = (int)getp(st, P_TRIGGER) == 0;
    const int  vel = (int)getp(st, P_VEL);
    const double hold = getp(st, P_HOLD);

    /* Columns are time within the generation, so a glider walking right
       is a figure walking later, and a still life is a chord. The
       column offset is a fraction of the period, which keeps a
       generation inside its own beat however long that beat is. */
    const double slot = period / (double)st->w;

    for (int x = 0; x < st->w; x++)
        for (int y = 0; y < st->h; y++)
        {
            const char *what = births ? &st->born[0] : &st->cells[0];

            if (!what[idx(st, x, y)])
                continue;

            /* Past the ladder's top, keep climbing in octaves -- the
               same wrapping every ladder in this tree does, so a board
               taller than its scale is a wider range rather than a
               truncated one. */
            const size_t n = st->ladder.size();
            const int    note = st->ladder[y % n] + 12 * (int)(y / n);

            thcEvent ev = {};

            ev.type = THC_EV_NOTE;
            ev.at = t->now + x * slot;
            ev.channel = 0;                 /* the sink routes          */
            ev.u.note.note = note;
            ev.u.note.velocity = vel;
            ev.u.note.duration = hold;

            out->emit(out->ctx, &ev);
        }

    return t->now + period;
}

/* ---- being clicked ----------------------------------------------------- */

/* Where the board sits inside the area composer_draw was given. Square
 * cells, centered, because a Life board with rectangular cells reads
 * wrong -- a glider stops looking like a glider. */
static void
layout (const State *st, double w, double h,
        double &cell, double &ox, double &oy)
{
    cell = w / st->w;

    if (h / st->h < cell)
        cell = h / st->h;

    if (cell < 1)
        cell = 1;

    ox = (w - cell * st->w) / 2;
    oy = (h - cell * st->h) / 2;
}

extern "C" THINK_PLUGIN_API void
composer_input (void *state, const thcInputEvent *ev)
{
    State *st = static_cast<State *>(state);

    refresh(st);

    if (st->w <= 0 || st->h <= 0)
        return;

    double cell, ox, oy;

    layout(st, ev->w, ev->h, cell, ox, oy);

    const int x = (int)((ev->x - ox) / cell);
    const int row = (int)((ev->y - oy) / cell);

    if (x < 0 || row < 0 || x >= st->w || row >= st->h)
        return;

    const int y = st->h - 1 - row;    /* drawn top-down, stored bottom-up */

    if (ev->type == THC_IN_RELEASE)
    {
        st->paintX = st->paintY = -1;
        return;
    }

    /* A press decides what the gesture paints -- the opposite of the
       cell it landed on -- and the drag then paints that one value
       everywhere it goes. Toggling per cell instead would make dragging
       back over your own line erase it, which is not what a drag on a
       Life board should mean. */
    if (ev->type == THC_IN_PRESS)
        st->paintTo = st->cells[idx(st, x, y)] ? 0 : 1;
    else if (st->paintX == x && st->paintY == y)
        return;                        /* same cell, still dragging     */

    st->paintX = x;
    st->paintY = y;
    st->cells[idx(st, x, y)] = st->paintTo;
    st->touched = true;
}

/* The board as text, for the host to write back into the file. Only the
 * board param has anything to say; everything else about this plugin is
 * already in the file, unchanged. */
extern "C" THINK_PLUGIN_API const char *
composer_capture (void *state, int index)
{
    State *st = static_cast<State *>(state);

    if (index != paramIndex[P_BOARD])
        return NULL;

    st->captured = boardToString(st);

    return st->captured.c_str();
}

/* ---- draw -------------------------------------------------------------- */

extern "C" THINK_PLUGIN_API void
composer_draw (void *state, cairo_t *cr, double w, double h)
{
    State *st = static_cast<State *>(state);

    refresh(st);

    if (st->w <= 0 || st->h <= 0 || w <= 0 || h <= 0)
        return;

    double cell, ox, oy;

    layout(st, w, h, cell, ox, oy);

    const bool grid = cell >= 6;

    for (int row = 0; row < st->h; row++)
    {
        const int y = st->h - 1 - row;

        for (int x = 0; x < st->w; x++)
        {
            const double px = ox + x * cell;
            const double py = oy + row * cell;

            /* A cell born this generation is bright, one that merely
               survived is dim: what you hear by default is the births,
               so the two have to look different or the picture and the
               sound disagree. */
            if (st->cells[idx(st, x, y)])
            {
                if (st->born[idx(st, x, y)])
                    cairo_set_source_rgba(cr, 0.65, 0.95, 0.7, 0.95);
                else
                    cairo_set_source_rgba(cr, 0.45, 0.62, 0.5, 0.6);

                cairo_rectangle(cr, px + 0.5, py + 0.5,
                                cell - 1, cell - 1);
                cairo_fill(cr);
            }
            else if (grid)
            {
                cairo_set_source_rgba(cr, 1, 1, 1, 0.07);
                cairo_rectangle(cr, px + 0.5, py + 0.5,
                                cell - 1, cell - 1);
                cairo_stroke(cr);
            }
        }
    }
}
