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
 * and it toggles, drag to paint a row of them, right-drag to erase, and
 * the next generation takes it from there. That is COMPOSITION_HANDOFF.md §7's argument for
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
 * And it *listens*. A note arriving from upstream lights the cell it
 * names, which is the same edit a click makes, arriving from the piece
 * instead of the hand. The mapping is the tick's own, run backwards: the
 * tick turns a row into a pitch and a column into a time, so receive
 * turns a pitch back into a row and a time back into a column. Play a
 * phrase into it and the phrase draws itself on the board -- its shape
 * in pitch, its rhythm in the columns -- and then Conway takes whatever
 * you left, exactly as he takes whatever you clicked. A line feeding
 * this is a line proposing a seed and getting an argument back.
 *
 * `listen' says what an arriving note does and `pass' whether the note
 * is also heard downstream, the same pair of words `markov' uses for the
 * same pair of questions. `listen = 0' is the pure generator this
 * started as, which is what `glider.gen' still wants. `pass' covers
 * notes and only notes: everything else a chain carries -- offs,
 * chanargs, structure edits -- goes through whatever it is set to,
 * because a stage that ate those would be a hole in the pipeline rather
 * than a stage in it.
 *
 * DETERMINISM. There is no randomness here at all unless `scatter' asks
 * for one, and that draws from the instance seed like everything else.
 * A board nobody clicked replays exactly; a board somebody clicked
 * replays given the same clicks. A board an upstream stage drew on
 * replays outright, because that stage is as deterministic as this one
 * -- listening costs nothing here, which is not true of live MIDI and is
 * the whole difference between the two.
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include <cairo.h>

#include "thcomposer.h"

enum { P_BOARD, P_WIDTH, P_HEIGHT, P_SCATTER, P_TRIGGER, P_WRAP,
       P_NOTES, P_PERIOD, P_HOLD, P_VEL, P_LISTEN, P_PASS, P_COUNT };

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

        /* Lighting rather than toggling is the default because an
           upstream line repeats itself, and a repeated note that toggled
           would spend half its visits switching a cell back off -- a
           phrase that draws nothing on average. Toggle is there for the
           case somebody wants a line to erase as well as write, which is
           a different and rarer instrument. */
        { "listen",  "what an arriving note does to the board: 0 nothing, "
          "1 light that cell, 2 toggle it", THC_PARAM_INT, 0, 2, 1,
          NULL, NULL },
        { "pass",    "1: the note that lit a cell is also heard",
          THC_PARAM_INT, 0, 1, 1, NULL, NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_GENERATOR | THC_TRANSFORMER);
    info->set_desc(info->host,
        "Conway's Game of Life; click the board, or play notes into it.");

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

    /* When the generation now on the board began, so an arriving note's
       time can be turned back into the column the tick would have played
       it from. Set by tick, read by receive; zero before the first tick,
       which is the right answer for a note that beats the board to the
       transport. */
    double genAt;
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
    st->genAt = 0;

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

    /* Where this generation started, for receive to measure against. */
    st->genAt = t->now;

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

/* ---- being played into -------------------------------------------------- */

/* Which row plays this pitch: the tick's ladder mapping, run backwards.
 *
 * The tick reads row y as `ladder[y % n] + 12 * (y / n)', so a pitch
 * belongs to a row when it is one of the ladder's degrees in one of the
 * octaves the board is tall enough to reach. Exact rather than nearest,
 * because "nearest" would make every wrong note land somewhere and the
 * board fill up with a line's accidents; a note the ladder cannot spell
 * is a note this stage has no row for, and dropping it is what keeps the
 * board a picture of the scale rather than of the melody's misses.
 *
 * Put a `gen::quantize' upstream if you want every note to land. That is
 * the piece's decision to state, not this plugin's to assume.
 */
static int
rowOf (const State *st, int note)
{
    /* One caveat, and it is the ladder's rather than this function's: a
       ladder spanning more than an octave makes two rows name the same
       pitch once the board is taller than it -- `48,50,52,55,57,60,62,64'
       reaches 60 at row 5 and again at row 8 -- and the lower row wins.
       So this is the tick's mapping run backwards exactly when the
       ladder covers the height, and an approximation of it otherwise.
       Give a board as many notes as it has rows and the question does
       not arise; colony.gen does. */
    const size_t n = st->ladder.size();

    if (n == 0)
        return -1;

    for (int y = 0; y < st->h; y++)
        if (st->ladder[y % n] + 12 * (int)(y / n) == note)
            return y;

    return -1;
}

extern "C" THINK_PLUGIN_API void
composer_receive (void *state, const thcEvent *ev, thcEventSink *out)
{
    State *st = static_cast<State *>(state);

    const int listen = (int)getp(st, P_LISTEN);

    if (ev->type == THC_EV_NOTE && listen != 0 && st->w > 0 && st->h > 0)
    {
        const int y = rowOf(st, ev->u.note.note);

        if (y >= 0)
        {
            const double period = getp(st, P_PERIOD);
            const double slot = period / (double)st->w;

            /* And the column: the tick's other mapping backwards. It
               plays column x at `genAt + x * slot', so a note at time
               `at' belongs in the column that time falls in. A phrase
               spread across a generation draws its own rhythm; one
               arriving faster than a slot piles into one column, which
               is the honest picture of playing faster than the board can
               see. Wrapped rather than clamped, because a line running
               past the end of a generation should come back round to
               the start of the next one; clamping would pile every late
               note against the right-hand edge. */
            int x = 0;
            const double into = ev->at - st->genAt;

            /* The range check is not paranoia about arithmetic; it is
               about the cast. Converting a NaN or a value past LONG_MAX
               to long is undefined, and `at' is a double that arrived
               from a plugin. The bound is generous -- a board would have
               to be days wide for a real column to reach it -- and
               anything outside it draws in column 0, which is a cell in
               the right row at the wrong time rather than a crash. */
            if (slot > 0 && into > -1e9 && into < 1e9)
            {
                const long col = (long)std::floor(into / slot);

                x = (int)(((col % st->w) + st->w) % st->w);
            }

            char *cell = &st->cells[idx(st, x, y)];

            *cell = (listen == 2 && *cell) ? 0 : 1;

            /* The same flag a click sets, and for the same reason: the
               board and the param no longer agree, and a Capture that
               round-trips must not be mistaken for somebody restating
               the board. */
            st->touched = true;
        }
    }

    /* `pass' is about the notes this stage eats, and nothing else.
     *
       It used to gate the emit for every event type, which meant a
       `pass = 0' board silently swallowed the note-offs, chanarg
       writes and structure edits of every stage upstream of it -- a
       gen::reshape in front of one delivered nothing at all. A stage
       consuming what it is *for* is a design; a stage consuming
       everything that happens to pass through it is a hole in a
       pipeline. */
    if (ev->type != THC_EV_NOTE || (int)getp(st, P_PASS) != 0)
        out->emit(out->ctx, ev);
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

    /* A press decides what the gesture paints and the drag then paints
       that one value everywhere it goes. Toggling per cell instead would
       make dragging back over your own line erase it, which is not what
       a drag on a Life board should mean.
     *
       The primary button paints the opposite of the cell it landed on,
       so a single click toggles and a drag extends whatever that first
       cell became. Any other button erases, always -- which is what a
       right-drag is for everywhere else, and it is why thcInputEvent
       carries a button at all rather than assuming there is one. */
    if (ev->type == THC_IN_PRESS)
        st->paintTo = (ev->button == 1)
            ? (st->cells[idx(st, x, y)] ? 0 : 1)
            : 0;
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

    /* What is playing now, which is what capture means -- and the board
       is only guaranteed current after a refresh. tick, draw and input
       all call one, so in practice it has usually happened; in practice
       is not a contract, and a host is entitled to ask a stage that has
       not been drawn or ticked since its params last moved. */
    refresh(st);

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
