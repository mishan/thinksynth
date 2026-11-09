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

#ifndef THG_LIFE_H
#define THG_LIFE_H

#include <stdint.h>

#include <string>
#include <vector>

#include "thhControls.h"
#include "thhMapper.h"

/* Conway's Life as a step sequencer: a playhead sweeps the board's
   columns, playing the live cells of each it reaches, and each time it
   comes round the board advances a generation, so the bar plays itself
   anew. No drawing and no sound in it: the playhead's cells come out as
   controls a profile maps (thgLifeControls), as composer::life's rows are
   degrees and its columns time. B3/S23 on a torus. Row 0 is the bottom,
   the lowest note. Deterministic for the same seed and inputs. */

/* A live cell the playhead reached. */
struct thgCell
{
    int  col, row;
    int  neighbors;             /* live, of the eight round it */
    bool born;                  /* by the last generation */
};

class thgLife
{
public:
    static const int COLS = 16, ROWS = 8;

    explicit thgLife (uint32_t seed);

    bool alive (int col, int row) const;
    int neighbors (int col, int row) const;
    int generation () const { return generation_; }

    /* The column the playhead is on, -1 before the first step. */
    int column () const { return column_; }

    /* Columns a second, 8 to start: sixteenths at 120 bpm. */
    void tempo (float steps) { tempo_ = steps; }
    float tempo () const { return tempo_; }

    /* Paused, the board holds still: a plain step sequencer. */
    void pause (bool p) { paused_ = p; }
    bool paused () const { return paused_; }

    /* dt seconds on, true if the playhead reached a column, its live
       cells put in cells. Coming round to the first column advances a
       generation first; paused, no cell counts as born then. The first
       step reaches it at once. A column a call at most, any more owed to
       the next call, so each column's notes are heard. */
    bool step (float dt, std::vector<thgCell> &cells);

    /* One generation, whatever the playhead. */
    void advance ();

    /* A cell toggled. A press toggles one too and a drag from it sets
       the cells it crosses the same, so a drag from a dead cell paints a
       line and one from a live cell erases it. */
    void tap (int col, int row);
    void press (int col, int row);
    void drag (int col, int row);

    /* A fifth of the cells, from the seed, brought to life: a board that
       died out, started again. */
    void shake ();

    /* The board in composer::life's spelling: `.' dead, `O' alive, `/'
       ends a row, the top row first. Read, anything but `.' or `0' is
       alive, short rows are dead to the end and long ones cut. */
    std::string board () const;
    void board (const std::string &text);

    /* The cell under (x, y) on a w by h screen the board fills, false
       off it. */
    static bool cellAt (float x, float y, float w, float h, int &col, int &row);

private:
    bool     cells_[COLS][ROWS], born_[COLS][ROWS];
    uint32_t rand_;
    float    tempo_, phase_;
    int      column_, generation_;
    bool     paused_, paint_;

    void set (int col, int row, bool on);
};

/* The board as a controller, a row at a time, as a ball is one of the
   marbles': `cell' is down while the playhead is on a live cell of that
   row, `born' too if the last generation brought it to life, `row' says
   which (0..1 from the bottom up), `column' where in the bar (0..1), and
   `neighbors' how crowded it is (0..1 of eight). */
enum { THG_LIFE_CELL, THG_LIFE_BORN };
enum { THG_LIFE_ROW, THG_LIFE_COLUMN, THG_LIFE_NEIGHBORS };

const thhControls &thgLifeControls ();

/* What Life plays with no block of a patch's: a row a degree of C minor
   pentatonic up from C4, more crowded louder. */
extern const char *const THG_LIFE_PROFILE;

/* A mapper for each row, so a column's live cells sound together, a
   chord. A cell's note lasts until the playhead leaves its column, and
   the same row's cell in the next column strikes it again: the notes
   follow the tempo with no clock of their own, and a line of cells is a
   rhythm, as a step sequencer's, not one long note. */
class thgLifePlayer
{
public:
    /* The notes the old profile held end, in out. */
    bool configure (const thcHidBlock &profile, std::vector<thhAction> &out,
                    std::string &err, int &errLine);

    /* The cells of the column a step reached, at time seconds. */
    void play (const std::vector<thgCell> &cells, double time, int curChannel,
               std::vector<thhAction> &out);

    void release (std::vector<thhAction> &out);

private:
    thhMapper mapper_[thgLife::ROWS];
    thhState  s_[thgLife::ROWS];
};

#endif /* THG_LIFE_H */
