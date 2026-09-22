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

/* grid -- the pattern you click.
 *
 * Rows are degrees on a pitch ladder, columns are steps, and a playhead
 * walks left to right forever. It is the oldest instrument in this
 * family and the one nothing here had: `gen::euclid' makes a rhythm out
 * of two integers, `gen::lsystem' makes a phrase out of a grammar, and
 * both are ways of not saying which notes you want. This one says them.
 *
 * THE PATTERN IS A STRING, spelled the way `gen::life' spells its board
 * and for the same reason -- a piece can ship one, `composer_capture'
 * can hand a clicked one back as the same text, and the whole thing
 * round-trips through the file with no opaque blob and no ABI for
 * saving state:
 *
 *   .   nothing on this step
 *   x   a note (O, # and 1 also work: patterns come from other hands)
 *   X   the same note, harder, by `accent' more velocity
 *   -   a tie: the note in this row goes on sounding through this step
 *   /   ends a row
 *
 * Rows are written top-down, highest degree first, because that is the
 * picture -- what the draw shows and what a click lands on. They are
 * stored bottom-up, because row 0 being the lowest note is what makes
 * the ladder read like a staff. `|' and spaces are ignored, so a bar
 * line may be written where one falls and a sixteen-step row can be read
 * without counting: "x..x|..x.|x...|..x." is the same pattern as
 * "x..x..x.x.....x." and easier to argue with.
 *
 * ONE COLUMN PER TICK, and this is the decision the plugin turns on.
 * `gen::steps' and `gen::life' emit a whole row or a whole generation at
 * once, a bar ahead, which is what puts a shape in the piano roll's
 * ghosted future -- and it means a cell you click is heard up to a bar
 * later. A grid is the thing somebody is clicking *while it plays*, so
 * this one emits the step it is on and sleeps until the next: a click
 * lands on the next step, and the playhead in the picture is where the
 * sound is rather than where the planning got to. Put a `gen::euclid'
 * or an `xform::run' in a chain that wants lookahead; they are still
 * the stages for it.
 *
 * TIES ARE LENGTH, not a second note. A step whose cell is `-' emits
 * nothing; the note that reaches it was emitted longer. So `x---' is
 * one note four steps long, which a sequence of note-offs could not say
 * and which is what the `hold' param alone never gave anyone: hold is a
 * length for every note in the pattern, and a tie is a length for this
 * one. A tie with no note in front of it in its row is silence, the way
 * a tie after a rest is in every other notation.
 *
 * AND IT LISTENS. A note arriving from upstream lights the cell it
 * names -- the ladder mapping run backwards for the row, the playhead
 * run backwards for the column -- so a chain with `input midi' in front
 * of a grid is a recorder: play a phrase and it draws itself on the
 * grid, quantized to the step it landed nearest, and then loops. That
 * is the same edit a click makes, arriving from the hands instead.
 * `listen = 0' turns it off, which is what a written pattern wants.
 *
 * DETERMINISM. There is no randomness here at all, and no use of the
 * instance seed: the pattern is the pattern. A grid nobody touched
 * replays exactly; a grid somebody clicked replays given the same
 * clicks, which is what makes a click a command in a room rather than a
 * secret.
 */

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef THC_NO_DRAW
#include <cairo.h>
#endif

#include "thcomposer.h"

enum { P_CELLS, P_STEPS, P_ROWS, P_NOTES, P_PERIOD, P_HOLD, P_VEL,
       P_ACCENT, P_LISTEN, P_PASS, P_COUNT };

static int paramIndex[P_COUNT];

#define MAX_STEPS 64
#define MAX_ROWS  32

/* What a cell holds. Stored rather than re-derived from the text so a
   click has somewhere to land. */
enum { CELL_OFF = 0, CELL_HIT, CELL_ACCENT, CELL_TIE };

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        /* The default is a bass figure rather than an empty grid,
           because an empty grid is silent and a plugin that does
           nothing until it is configured is one nobody hears. Four on
           the floor with the fifth on the offbeats, over the default
           ladder, which is C major pentatonic across two octaves. */
        { "cells",  "the pattern: . nothing, x a note, X accented, "
          "- a tie, / ends a row",
          THC_PARAM_STRING, 0, 0, 0,
          "................/"
          "................/"
          "........x......./"
          "..............x./"
          "..x...x...x...x./"
          "................/"
          "................/"
          "x...x...x...x...", NULL },
        { "steps",  "steps in the pattern", THC_PARAM_INT,
          1, MAX_STEPS, 16, NULL, NULL },
        { "rows",   "rows; each one is a degree of the ladder",
          THC_PARAM_INT, 1, MAX_ROWS, 8, NULL, NULL },
        { "notes",  "pitch ladder; row 0 is the bottom",
          THC_PARAM_NOTESET, 0, 0, 0, "48,50,52,55,57,60,62,64", NULL },
        { "period", "length of one step", THC_PARAM_FLOAT,
          0.02, 60, 0.25, NULL, "s" },
        { "hold",   "time before note-off, before any ties",
          THC_PARAM_FLOAT, 0.01, 60, 0.2, NULL, "s" },
        { "vel",    "velocity", THC_PARAM_INT, 1, 127, 96, NULL, NULL },
        { "accent", "extra velocity on an X cell", THC_PARAM_INT,
          0, 127, 24, NULL, NULL },

        /* Lighting rather than toggling is the default for the reason
           gen::life gives: a line repeats itself, and a repeated note
           that toggled would spend half its visits switching a cell
           back off -- a phrase that draws nothing on average. */
        { "listen", "what an arriving note does: 0 nothing, 1 light "
          "that cell, 2 toggle it", THC_PARAM_INT, 0, 2, 1, NULL, NULL },
        { "pass",   "1: the note that lit a cell is also heard",
          THC_PARAM_INT, 0, 1, 1, NULL, NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_GENERATOR | THC_TRANSFORMER);
    info->set_desc(info->host,
        "A step grid: click the pattern, or play notes into it.");

    return 0;
}

/* ---- the pattern ------------------------------------------------------- */

struct State {
    const thcParams *params;

    int steps, rows;
    std::vector<char> cells;     /* steps * rows, row 0 at the bottom   */

    std::vector<int>  ladder;
    std::string       ladderText;

    std::string cellsText;       /* what cells was built from           */
    std::string captured;        /* what composer_capture last returned */

    /* Where the playhead is, and when the step it is on began, so an
       arriving note's time can be turned back into a column. */
    int    pos;
    double posAt;

    /* Set by a click or by a note landing, cleared once the param has
       been re-read: the grid and the text no longer agree, and the one
       in front of somebody is the grid. */
    bool touched;

    /* The last cell a drag painted and what it painted, so dragging
       across a row paints one value rather than flickering. */
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
    return y * st->steps + x;
}

/* One character of the pattern. Everything that is not a cell has been
   filtered out before this is asked. */
static char
cellOf (char c)
{
    switch (c)
    {
    case '.': case '0': return CELL_OFF;
    case 'X':           return CELL_ACCENT;
    case '-': case '_': return CELL_TIE;
    default:            return CELL_HIT;
    }
}

static char
charOf (char cell)
{
    switch (cell)
    {
    case CELL_ACCENT: return 'X';
    case CELL_TIE:    return '-';
    case CELL_HIT:    return 'x';
    default:          return '.';
    }
}

/* Text to grid. Short rows are padded empty and long ones truncated,
 * because the size is the params' business: a pattern pasted in from a
 * wider grid should still land, one row at a time, rather than fail.
 */
static void
parseCells (State *st, const std::string &text)
{
    st->cells.assign((size_t)st->steps * st->rows, CELL_OFF);

    int x = 0, row = 0;

    for (size_t i = 0; i < text.size() && row < st->rows; i++)
    {
        const char c = text[i];

        if (c == '/' || c == '\n')
        {
            row++;
            x = 0;
            continue;
        }

        /* Bar lines and spaces are for the reader. A row that used them
           to group its steps has to come out the same length as one
           that did not, so they cost a column nothing. */
        if (c == '|' || c == ' ' || c == '\t' || c == '\r')
            continue;

        if (x < st->steps)
            st->cells[idx(st, x, st->rows - 1 - row)] = cellOf(c);

        x++;
    }
}

static std::string
cellsToString (const State *st)
{
    std::string out;

    for (int row = 0; row < st->rows; row++)
    {
        const int y = st->rows - 1 - row;

        if (row)
            out += '/';

        for (int x = 0; x < st->steps; x++)
            out += charOf(st->cells[idx(st, x, y)]);
    }

    return out;
}

/* Degrees only, which is to say 0..127: a `.' in a note list resolves to
   -1 at load and is not a degree, and every ladder in the tree filters
   what it is handed the same way. */
static void
parseLadder (State *st, const char *text)
{
    st->ladder.clear();

    const char *p = text;

    while (p && *p)
    {
        char *end = NULL;
        const long v = strtol(p, &end, 10);

        if (end == p)
            break;

        if (v >= 0 && v <= 127)
            st->ladder.push_back((int)v);

        p = (*end == ',') ? end + 1 : end;
    }

    if (st->ladder.empty())
        st->ladder.push_back(60);
}

/* Rebuilt when the size or the written pattern changed -- but never on
 * top of a pattern somebody clicked, which is the one thing here that
 * has no other copy.
 */
static void
refresh (State *st)
{
    int steps = (int)getp(st, P_STEPS);
    int rows = (int)getp(st, P_ROWS);

    if (steps < 1) steps = 1;
    if (steps > MAX_STEPS) steps = MAX_STEPS;
    if (rows < 1) rows = 1;
    if (rows > MAX_ROWS) rows = MAX_ROWS;

    const char *cells =
        st->params->get_string(st->params->ctx, paramIndex[P_CELLS]);
    const char *notes =
        st->params->get_string(st->params->ctx, paramIndex[P_NOTES]);

    const std::string cellsText = cells ? cells : "";
    const std::string notesText = notes ? notes : "";

    if (notesText != st->ladderText || st->ladder.empty())
    {
        st->ladderText = notesText;
        parseLadder(st, notesText.c_str());
    }

    const bool resized = (steps != st->steps || rows != st->rows);

    if (!resized && cellsText == st->cellsText && !st->cells.empty())
        return;

    /* A pattern that was clicked, and a param that has since changed.
     *
     * Two different things arrive here and they want opposite answers.
     * The host writing back what this plugin just handed it -- a
     * Capture -- must not reparse: the text and the grid already agree.
     * Anything else is somebody stating a pattern in the file or the
     * panel, and that wins, because a click that outranked every later
     * edit would be a pattern nobody could correct.
     *
     * Telling them apart is exact rather than heuristic, because the
     * plugin remembers the last text it gave out. */
    if (!resized && st->touched && cellsText == st->captured)
    {
        st->cellsText = cellsText;
        return;
    }

    st->steps = steps;
    st->rows = rows;
    st->cellsText = cellsText;
    st->touched = false;

    parseCells(st, cellsText);

    if (st->pos >= st->steps)
        st->pos = 0;
}

extern "C" THINK_PLUGIN_API void *
composer_create (const thcParams *params)
{
    State *st = new State;

    st->params = params;
    st->steps = st->rows = 0;
    st->pos = 0;
    st->posAt = 0;
    st->touched = false;
    st->paintX = st->paintY = -1;
    st->paintTo = CELL_OFF;

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

/* ---- playing it -------------------------------------------------------- */

/* The pitch of row y: the ladder, climbing in octaves past its top, the
   same wrapping every ladder in this tree does -- so a grid taller than
   its scale is a wider range rather than a truncated one. */
static int
noteOf (const State *st, int y)
{
    const size_t n = st->ladder.size();

    return st->ladder[y % n] + 12 * (int)(y / n);
}

/* How many steps the note starting at column x in row y is held for: its
 * own, plus every tie that follows it. The scan stops at the end of the
 * pattern rather than wrapping, because a note tied past the last step
 * would have to be shortened again the moment somebody changed the
 * first one, and "the tie runs to the end" is a rule that survives an
 * edit. */
static int
tiedLength (const State *st, int x, int y)
{
    int n = 1;

    while (x + n < st->steps &&
           st->cells[idx(st, x + n, y)] == CELL_TIE)
        n++;

    return n;
}

extern "C" THINK_PLUGIN_API double
composer_tick (void *state, const thcTransport *t, thcEventSink *out)
{
    State *st = static_cast<State *>(state);

    refresh(st);

    const double period = getp(st, P_PERIOD);

    if (st->steps <= 0 || st->rows <= 0 || !t->running)
        return t->now + period;

    const int vel = (int)getp(st, P_VEL);
    const int accent = (int)getp(st, P_ACCENT);
    const double hold = getp(st, P_HOLD);

    /* Where this step started, for receive to measure against. */
    st->posAt = t->now;

    for (int y = 0; y < st->rows; y++)
    {
        const char cell = st->cells[idx(st, st->pos, y)];

        if (cell != CELL_HIT && cell != CELL_ACCENT)
            continue;

        /* A tie only lengthens the note it follows, so a hit that lands
           on one somebody drew mid-run is still this step's note. */
        const int held = tiedLength(st, st->pos, y);

        int v = cell == CELL_ACCENT ? vel + accent : vel;

        if (v > 127) v = 127;
        if (v < 1) v = 1;

        thcEvent ev = {};

        ev.type = THC_EV_NOTE;
        ev.at = t->now;
        ev.channel = 0;                    /* the sink routes           */
        ev.u.note.note = noteOf(st, y);
        ev.u.note.velocity = v;
        ev.u.note.duration = hold + (held - 1) * period;

        out->emit(out->ctx, &ev);
    }

    st->pos = (st->pos + 1) % st->steps;

    return t->now + period;
}

/* ---- being played into -------------------------------------------------- */

/* Which row plays this pitch: noteOf run backwards.
 *
 * Exact rather than nearest, because "nearest" would make every wrong
 * note land somewhere and the grid fill up with a line's accidents. A
 * note the ladder cannot spell is a note this stage has no row for, and
 * dropping it is what keeps the picture a picture of the scale. Put a
 * `gen::quantize' upstream if you want every note to land; that is the
 * piece's decision to state, not this plugin's to assume.
 */
static int
rowOf (const State *st, int note)
{
    for (int y = 0; y < st->rows; y++)
        if (noteOf(st, y) == note)
            return y;

    return -1;
}

extern "C" THINK_PLUGIN_API void
composer_receive (void *state, const thcEvent *ev, thcEventSink *out)
{
    State *st = static_cast<State *>(state);

    const int listen = (int)getp(st, P_LISTEN);

    if (ev->type == THC_EV_NOTE && listen != 0 &&
        st->steps > 0 && st->rows > 0)
    {
        const int y = rowOf(st, ev->u.note.note);

        if (y >= 0)
        {
            const double period = getp(st, P_PERIOD);

            /* And the column: the playhead run backwards. The step this
               grid is on began at posAt, so a note at `at' is that many
               periods along -- rounded to the *nearest* step rather
               than the one it fell inside, because somebody playing
               along is trying to hit the beat and landing a little
               either side of it. That is what quantizing means, and it
               is the whole difference between a recorder and a log.

               The range check is about the cast: converting a NaN or a
               value past LONG_MAX to long is undefined, and `at' is a
               double that arrived from a plugin. Anything outside the
               bound lands on the step the playhead is on, which is a
               cell in the right row at a defensible time. */
            int x = st->pos;
            const double into = ev->at - st->posAt;

            if (period > 0 && into > -1e9 && into < 1e9)
            {
                const long col = st->pos + (long)std::floor(into / period
                                                            + 0.5);

                x = (int)(((col % st->steps) + st->steps) % st->steps);
            }

            char *cell = &st->cells[idx(st, x, y)];

            *cell = (listen == 2 && *cell != CELL_OFF) ? CELL_OFF
                                                       : CELL_HIT;

            /* The same flag a click sets, and for the same reason: the
               grid and the param no longer agree, and a Capture that
               round-trips must not be mistaken for somebody restating
               the pattern. */
            st->touched = true;
        }
    }

    /* `pass' is about the notes this stage eats, and nothing else: a
       stage consuming what it is for is a design, and one consuming
       every chanarg and structure edit that happens through it is a
       hole in the pipeline. The off goes with its on, because the
       scheduler hands an unmatched off to delNote and that silences
       whatever else is sounding at that pitch. */
    if ((ev->type != THC_EV_NOTE && ev->type != THC_EV_NOTEOFF) ||
        (int)getp(st, P_PASS) != 0)
        out->emit(out->ctx, ev);
}

/* ---- being clicked ----------------------------------------------------- */

/* Where the grid sits inside the area composer_draw was given.
 *
 * The whole of it, and cells that are not square -- which is the
 * difference between this and a Life board. A board with rectangular
 * cells reads wrong because a glider stops looking like a glider;
 * sixteen steps of a pattern across a pane that is wider than it is
 * tall are simply sixteen columns, and squaring them would leave the
 * pane mostly empty for the sake of a symmetry nothing here has.
 */
static void
layout (const State *st, double w, double h, double &cw, double &ch)
{
    cw = w / st->steps;
    ch = h / st->rows;
}

extern "C" THINK_PLUGIN_API void
composer_input (void *state, const thcInputEvent *ev)
{
    State *st = static_cast<State *>(state);

    refresh(st);

    if (st->steps <= 0 || st->rows <= 0 || ev->w <= 0 || ev->h <= 0)
        return;

    double cw, ch;

    layout(st, ev->w, ev->h, cw, ch);

    const int x = (int)(ev->x / cw);
    const int row = (int)(ev->y / ch);

    if (x < 0 || row < 0 || x >= st->steps || row >= st->rows)
        return;

    const int y = st->rows - 1 - row;    /* drawn top-down, stored up   */

    if (ev->type == THC_IN_RELEASE)
    {
        st->paintX = st->paintY = -1;
        return;
    }

    /* A press decides what the gesture paints and the drag then paints
       that one value everywhere it goes. Toggling per cell instead
       would make dragging back over your own line erase it.
     *
       The primary button cycles the cell it landed on: nothing becomes
       a note, a note becomes an accent, an accent becomes nothing
       again. Three states is one more than a Life board has and it is
       the one the ear asks for first -- a pattern with nothing louder
       in it than anything else is a pattern with no beat. Any other
       button erases, always, which is what a right-drag means
       everywhere else. A tie is not on the cycle: it belongs to the
       note before it rather than to the cell it is in, so it is a drag
       along a row and not a click, and until that gesture exists it is
       written in the text. */
    if (ev->type == THC_IN_PRESS)
    {
        const char was = st->cells[idx(st, x, y)];

        if (ev->button != 1)
            st->paintTo = CELL_OFF;
        else if (was == CELL_OFF || was == CELL_TIE)
            st->paintTo = CELL_HIT;
        else if (was == CELL_HIT)
            st->paintTo = CELL_ACCENT;
        else
            st->paintTo = CELL_OFF;
    }
    else if (st->paintX == x && st->paintY == y)
        return;                          /* same cell, still dragging   */

    st->paintX = x;
    st->paintY = y;
    st->cells[idx(st, x, y)] = st->paintTo;
    st->touched = true;
}

/* The pattern as text, for the host to write back into the file. Only
   the cells param has anything to say; everything else about this
   plugin is already in the file, unchanged. */
extern "C" THINK_PLUGIN_API const char *
composer_capture (void *state, int index)
{
    State *st = static_cast<State *>(state);

    if (index != paramIndex[P_CELLS])
        return NULL;

    /* What is playing now, which is what capture means -- and the grid
       is only guaranteed current after a refresh. tick, draw and input
       all call one, so in practice it has usually happened; in practice
       is not a contract. */
    refresh(st);

    st->captured = cellsToString(st);

    return st->captured.c_str();
}

/* ---- draw -------------------------------------------------------------- */

#ifndef THC_NO_DRAW
extern "C" THINK_PLUGIN_API void
composer_draw (void *state, cairo_t *cr, double w, double h)
{
    State *st = static_cast<State *>(state);

    refresh(st);

    if (st->steps <= 0 || st->rows <= 0 || w <= 0 || h <= 0)
        return;

    double cw, ch;

    layout(st, w, h, cw, ch);

    const bool outline = cw >= 5 && ch >= 5;

    for (int row = 0; row < st->rows; row++)
    {
        const int y = st->rows - 1 - row;

        for (int x = 0; x < st->steps; x++)
        {
            const char cell = st->cells[idx(st, x, y)];
            const double px = x * cw;
            const double py = row * ch;

            if (cell == CELL_OFF)
            {
                if (outline)
                {
                    cairo_set_source_rgba(cr, 1, 1, 1, 0.07);
                    cairo_rectangle(cr, px + 0.5, py + 0.5,
                                    cw - 1, ch - 1);
                    cairo_stroke(cr);
                }

                continue;
            }

            /* A tie is drawn as the tail of the note it belongs to
               rather than as a cell of its own -- inset top and bottom,
               full width, so a held note reads as one long bar across
               the steps it covers. That is the picture the text is
               trying to be. */
            if (cell == CELL_TIE)
            {
                /* Starting a pixel to the left of its own cell, so the
                   tie and the note it belongs to are one shape rather
                   than a note and a bar with a seam between them. */
                cairo_set_source_rgba(cr, 0.55, 0.78, 0.95, 0.55);
                cairo_rectangle(cr, px - 1, py + ch * 0.3,
                                cw + 1, ch * 0.4);
                cairo_fill(cr);
                continue;
            }

            if (cell == CELL_ACCENT)
                cairo_set_source_rgba(cr, 1.0, 0.85, 0.35, 0.95);
            else
                cairo_set_source_rgba(cr, 0.55, 0.78, 0.95, 0.9);

            cairo_rectangle(cr, px + 0.5, py + 0.5, cw - 1, ch - 1);
            cairo_fill(cr);
        }
    }

    /* The column the playhead is on is the one about to sound: pos has
       already been advanced past the step that just did. Washed over
       the cells rather than laid under them, because a drum track is a
       row with every cell filled and a highlight behind those is a
       highlight nobody sees. */
    cairo_set_source_rgba(cr, 1, 1, 1, 0.16);
    cairo_rectangle(cr, st->pos * cw, 0, cw, h);
    cairo_fill(cr);

    /* Every fourth step, over the cells rather than under them: drawn
       first, the empty cells' own outlines covered them and sixteen
       columns were sixteen identical columns. Beats and not bars,
       because the grid does not know what a bar is -- its steps may be
       seconds. */
    if (cw >= 3)
    {
        cairo_set_source_rgba(cr, 1, 1, 1, 0.35);
        cairo_set_line_width(cr, 1);

        for (int x = 4; x < st->steps; x += 4)
        {
            cairo_move_to(cr, x * cw + 0.5, 0);
            cairo_line_to(cr, x * cw + 0.5, h);
        }

        cairo_stroke(cr);
    }
}
#endif
