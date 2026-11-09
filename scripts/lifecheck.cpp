/*
 * Copyright (C) 2004-2026 The thinksynth authors
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
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * lifecheck -- Life as a step sequencer (game/thgLife.h): B3/S23 on known
 * patterns, the playhead and its wrap, painting, the shake, the board's
 * text, and the chords its controls play through a mapper.
 */

#include <cstdio>
#include <string>
#include <vector>

#include "thcHidBlock.h"
#include "thgLife.h"
#include "thhMapper.h"

static int failed = 0;

static void
report (const char *name, bool ok, const std::string &why)
{
    if (ok)
        printf("ok    %s\n", name);
    else
    {
        printf("FAIL  %s\n  %s\n", name, why.c_str());
        failed++;
    }
}

static thgLife
from (const char *text)
{
    thgLife l(1);

    l.board(text);
    return l;
}

/* l's board moved dc across and dr up, round the torus. */
static std::string
moved (const thgLife &l, int dc, int dr)
{
    thgLife m(1);

    for (int c = 0; c < thgLife::COLS; c++)
        for (int r = 0; r < thgLife::ROWS; r++)
            if (l.alive(c, r))
                m.tap((c + dc + thgLife::COLS) % thgLife::COLS,
                      (r + dr + thgLife::ROWS) % thgLife::ROWS);
    return m.board();
}

static std::string
rows (const std::vector<thgCell> &cells)
{
    std::string s;

    for (const thgCell &c : cells)
        s += std::to_string(c.row) + (c.born ? "b" : "") + " ";
    return s;
}

int
main ()
{
    /* Each pattern after so many generations, against where it should
       be: oscillators back, still lifes still, gliders a cell down and
       across every four, the torus carrying one over its edges. */
    {
        struct R { const char *name, *start; int gens, dc, dr; const char *want; };
        static const R RULE[] = {
            { "a blinker turns", "/.O/.O/.O", 1, 0, 0, "//OOO" },
            { "a blinker is back in two", "/.O/.O/.O", 2, 0, 0, NULL },
            { "a block stays", "OO/OO", 5, 0, 0, NULL },
            { "a beehive stays", ".OO/O..O/.OO", 3, 0, 0, NULL },
            { "a glider moves a cell down and across in four", ".O/..O/OOO", 4, 1, -1, NULL },
            { "a glider crosses the corner of the torus",
              "/////O/.O/OO.............O", 4, 1, -1, NULL },
            { "a lone cell dies", "/.O", 1, 0, 0, "" },
        };

        for (const R &r : RULE)
        {
            thgLife l = from(r.start);
            const std::string want = r.want ? from(r.want).board() : moved(l, r.dc, r.dr);

            for (int g = 0; g < r.gens; g++)
                l.advance();
            report(r.name, l.board() == want, l.board() + "\n  wanted " + want);
        }
    }

    /* The playhead at 4 columns a second, a quarter second a step, over
       a vertical blinker in column 1: each column's live cells, and at
       the wrap a generation, unless paused. */
    {
        struct R { const char *name; bool paused; int gen; const char *col0, *col1; };
        static const R WRAP[] = {
            { "the wrap advances a generation", false, 1, "3b ", "3 " },
            { "paused, the wrap plays the same board", true, 0, "", "2 3 4 " },
        };

        for (const R &r : WRAP)
        {
            thgLife l = from("///.O/.O/.O");
            std::vector<thgCell> cells;
            std::string got, want = "0: 1: 2 3 4 2: ";
            bool between;

            l.tempo(4);
            l.pause(r.paused);
            for (int c = 0; c < thgLife::COLS; c++)
            {
                if (!l.step(c ? 0.25f : 0, cells))
                    break;
                if (c < 3)
                    got += std::to_string(l.column()) + ": " + rows(cells);
            }
            between = l.step(0.125f, cells);
            l.step(0.125f, cells);
            got += "/ " + std::to_string(l.generation()) + " 0: " + rows(cells);
            l.step(0.25f, cells);
            got += "1: " + rows(cells);
            want += "/ " + std::to_string(r.gen) + " 0: " + r.col0 + "1: " + r.col1;
            report(r.name, got == want && !between, got + "\n  wanted " + want);
        }
    }

    /* Paused, the wrap brings no generation, so no cell is born again:
       a vertical blinker turned, then the same board played paused. */
    {
        thgLife l = from("///.O/.O/.O");
        std::vector<thgCell> cells;
        std::string first;

        l.tempo(4);
        l.step(0, cells);
        for (int c = 0; c < thgLife::COLS; c++)
            l.step(0.25f, cells);
        first = rows(cells);
        l.pause(true);
        for (int c = 0; c < thgLife::COLS; c++)
            l.step(0.25f, cells);
        report("paused, the wrap plays no cell born again",
               first == "3b " && rows(cells) == "3 ", first + "then " + rows(cells));
    }

    /* A second's step at 4 a second reaches one column, and owes the
       rest: each of them is still played. */
    {
        thgLife l(1);
        std::vector<thgCell> cells;
        std::string got;

        l.tempo(4);
        l.step(0, cells);
        l.step(1, cells);
        for (int i = 0; i < 4; i++)
        {
            const bool reached = l.step(0, cells);

            got += std::to_string(reached) + std::to_string(l.column()) + " ";
        }
        report("a long step reaches a column, and the next owe the rest",
               got == "12 13 14 04 ", got);
    }

    /* Strokes on an empty board: a press and drags, the cells left. */
    {
        struct R { const char *name; const char *strokes; const char *want; };
        static const R PAINT[] = {
            { "a tap lights a cell, a second puts it out", "t11 t22 t22", "//////.O" },
            { "a drag from a dead cell paints a line", "p13 d23 d33 d43", "////.OOOO" },
            { "a drag from a live cell erases", "p13 d23 d33 d43 p23 d33 d43 d53", "////.O" },
            { "dragging back over a painted cell leaves it lit", "p13 d23 d13 d23", "////.OO" },
        };

        for (const R &r : PAINT)
        {
            thgLife l(1);
            const std::string strokes = r.strokes;

            for (size_t i = 0; i < strokes.size(); i += 4)
            {
                const int c = strokes[i + 1] - '0', w = strokes[i + 2] - '0';

                strokes[i] == 't' ? l.tap(c, w) : strokes[i] == 'p' ? l.press(c, w)
                                                                    : l.drag(c, w);
            }
            report(r.name, l.board() == from(r.want).board(),
                   l.board() + "\n  wanted " + from(r.want).board());
        }
    }

    /* The seed decides the shake: the same board for the same seed, not
       for another; a modest share of the board; and nothing put out. */
    {
        thgLife a(7), b(7), c(8);
        int lit = 0;

        a.board("O");
        b.board("O");
        c.board("O");
        a.shake();
        b.shake();
        c.shake();
        for (int col = 0; col < thgLife::COLS; col++)
            for (int row = 0; row < thgLife::ROWS; row++)
                lit += a.alive(col, row);
        report("a shake is the seed's, a modest share, and puts out none",
               a.board() == b.board() && a.board() != c.board() && lit > 12 && lit < 40 &&
               a.alive(0, thgLife::ROWS - 1),
               std::to_string(lit) + " lit\n  " + a.board() + "\n  " + c.board());
    }

    /* The board's text read and written: composer::life's spelling, the
       top row first, short rows dead to the end and long ones cut. */
    {
        static const char *const GLIDER =
            ".O............../..O............./OOO............./................/"
            "................/................/................/................";
        struct R { const char *name, *in, *want; };
        static const R TEXT[] = {
            { "a glider's text is the same written back", GLIDER, GLIDER },
            { "short rows and a short board are dead to the end", ".O/..O\nOOO", GLIDER },
            { "anything but . or 0 is alive", "0#/00*/X1Z", GLIDER },
            { "a long row is cut", ".O..............OOOO/..O/OOO", GLIDER },
        };

        for (const R &r : TEXT)
        {
            const std::string got = from(r.in).board();

            report(r.name, got == r.want, got);
        }

        thgLife l = from(GLIDER);

        report("the top row written first is the board's top", l.alive(1, 7) && l.alive(0, 5),
               l.board());
    }

    /* Where a touch lands, on a 320 by 240 screen: the top left the top
       row's first cell. */
    {
        struct R { float x, y; bool on; int col, row; };
        static const R AT[] = {
            { 0, 0, true, 0, 7 }, { 319, 239, true, 15, 0 }, { 25, 35, true, 1, 6 },
            { 320, 10, false, 0, 0 }, { 10, -1, false, 0, 0 },
        };
        std::string bad;

        for (const R &r : AT)
        {
            int col = -1, row = -1;
            const bool on = thgLife::cellAt(r.x, r.y, 320, 240, col, row);

            if (on != r.on || (on && (col != r.col || row != r.row)))
                bad += std::to_string(r.x) + "," + std::to_string(r.y) + " -> " +
                       std::to_string(col) + "," + std::to_string(row) + "; ";
        }
        report("a touch's cell on the screen", bad.empty(), bad);
    }

    /* Life's own profile through a mapper a row: a column's cells a
       chord of the scale's degrees up from C4, each ended when the
       playhead leaves the column, and struck again by the next column's
       cell in the same row. */
    {
        thcHidBlock p;
        thgLifePlayer player;
        std::vector<thhAction> acts;
        std::string err, got;
        int line;
        const bool ok = thcHidParseProfile(THG_LIFE_PROFILE, p, err, line) &&
                        player.configure(p, acts, err, line);
        /* Rows 0, 2, 4 and 7 in column 0; row 2 again in column 1. */
        thgLife l = from("O///O//OO//O");
        std::vector<thgCell> cells;
        double t = 0;

        l.tempo(4);
        for (int c = 0; c < 3; c++)
        {
            acts.clear();
            l.step(c ? 0.25f : 0, cells);
            player.play(cells, t += 0.25, 0, acts);
            got += std::to_string(c) + ":";
            for (const thhAction &a : acts)
                if (a.kind == thhAction::NOTE_ON || a.kind == thhAction::NOTE_OFF)
                    got += std::string(a.kind == thhAction::NOTE_ON ? " +" : " -") +
                           std::to_string(a.note);
            got += " ";
        }
        /* minor pentatonic: C Eb F G Bb; row 7 the octave's F. */
        const std::string want = "0: +60 +65 +70 +77 1: -60 -65 -70 -77 +65 2: -65 ";

        report("the profile plays a column as a chord, ended as the playhead leaves",
               ok && got == want, ok ? got + "\n  wanted " + want : err);
    }

    printf("%d failure%s\n", failed, failed == 1 ? "" : "s");
    return failed ? 1 : 0;
}
