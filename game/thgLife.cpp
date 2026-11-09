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

#include <cstring>

#include "thgLife.h"

/* `row' is 0..1 in sevenths, so a range of 7 is a degree a row: an
   octave and a half of the scale up the board. */
const char *const THG_LIFE_PROFILE =
    "device life {\n"
    "    base = \"C4\";\n"
    "    cell = note 0;\n"
    "    row = pitch { scale = minor_pentatonic; range = 7; };\n"
    "    neighbors = velocity { min = 60; max = 127; };\n"
    "};\n";

const thhControls &
thgLifeControls ()
{
    static const thhControls c = {
        "life",
        { "cell", "born" },
        { { "row", false }, { "column", false }, { "neighbors", false } },
    };

    return c;
}

thgLife::thgLife (uint32_t seed)
    : rand_(seed ? seed : 1), tempo_(8), phase_(0), column_(-1), generation_(0),
      paused_(false), paint_(true)
{
    memset(cells_, 0, sizeof(cells_));
    memset(born_, 0, sizeof(born_));
}

bool
thgLife::alive (int col, int row) const
{
    return cells_[(col % COLS + COLS) % COLS][(row % ROWS + ROWS) % ROWS];
}

int
thgLife::neighbors (int col, int row) const
{
    int n = 0;

    for (int dc = -1; dc <= 1; dc++)
        for (int dr = -1; dr <= 1; dr++)
            n += (dc || dr) && alive(col + dc, row + dr);
    return n;
}

void
thgLife::advance ()
{
    bool next[COLS][ROWS];

    for (int c = 0; c < COLS; c++)
        for (int r = 0; r < ROWS; r++)
        {
            const int n = neighbors(c, r);

            next[c][r] = n == 3 || (n == 2 && cells_[c][r]);
            born_[c][r] = next[c][r] && !cells_[c][r];
        }
    memcpy(cells_, next, sizeof(cells_));
    generation_++;
}

bool
thgLife::step (float dt, std::vector<thgCell> &cells)
{
    const bool first = column_ < 0;

    cells.clear();
    if (!first)
    {
        phase_ += dt * tempo_;
        if (phase_ < 1)
            return false;
        phase_ -= 1;
    }
    column_ = (column_ + 1) % COLS;
    /* Paused, the bar comes round with no generation: none is born. */
    if (column_ == 0 && !first && paused_)
        memset(born_, 0, sizeof(born_));
    else if (column_ == 0 && !first)
        advance();
    for (int r = 0; r < ROWS; r++)
        if (cells_[column_][r])
            cells.push_back({ column_, r, neighbors(column_, r), born_[column_][r] });
    return true;
}

void
thgLife::set (int col, int row, bool on)
{
    if (col < 0 || col >= COLS || row < 0 || row >= ROWS)
        return;
    cells_[col][row] = on;
    born_[col][row] = false;
}

void
thgLife::tap (int col, int row)
{
    if (col >= 0 && col < COLS && row >= 0 && row < ROWS)
        set(col, row, !cells_[col][row]);
}

void
thgLife::press (int col, int row)
{
    if (col < 0 || col >= COLS || row < 0 || row >= ROWS)
        return;
    paint_ = !cells_[col][row];
    set(col, row, paint_);
}

void
thgLife::drag (int col, int row)
{
    set(col, row, paint_);
}

/* xorshift32, as the marbles'. */
void
thgLife::shake ()
{
    for (int c = 0; c < COLS; c++)
        for (int r = 0; r < ROWS; r++)
        {
            rand_ ^= rand_ << 13;
            rand_ ^= rand_ >> 17;
            rand_ ^= rand_ << 5;
            if (rand_ % 5 == 0)
                set(c, r, true);
        }
}

std::string
thgLife::board () const
{
    std::string out;

    for (int r = ROWS - 1; r >= 0; r--)
    {
        for (int c = 0; c < COLS; c++)
            out += cells_[c][r] ? 'O' : '.';
        if (r)
            out += '/';
    }
    return out;
}

void
thgLife::board (const std::string &text)
{
    int c = 0, r = ROWS - 1;

    memset(cells_, 0, sizeof(cells_));
    memset(born_, 0, sizeof(born_));
    for (size_t i = 0; i < text.size() && r >= 0; i++)
    {
        const char ch = text[i];

        if (ch == '/' || ch == '\n')
        {
            r--;
            c = 0;
        }
        else if (ch != ' ' && ch != '\r' && c++ < COLS)
            cells_[c - 1][r] = ch != '.' && ch != '0';
    }
}

bool
thgLife::cellAt (float x, float y, float w, float h, int &col, int &row)
{
    if (x < 0 || y < 0 || x >= w || y >= h)
        return false;
    col = (int)(x / w * COLS);
    row = ROWS - 1 - (int)(y / h * ROWS);
    return true;
}

bool
thgLifePlayer::configure (const thcHidBlock &profile, std::vector<thhAction> &out,
                          std::string &err, int &errLine)
{
    for (int r = 0; r < thgLife::ROWS; r++)
    {
        if (!mapper_[r].configure(profile, thgLifeControls(), out, err, errLine))
            return false;
        s_[r].connected = true;
    }
    return true;
}

void
thgLifePlayer::play (const std::vector<thgCell> &cells, double time, int curChannel,
                     std::vector<thhAction> &out)
{
    for (int r = 0; r < thgLife::ROWS; r++)
    {
        s_[r].buttons = 0;
        s_[r].time = time;
        mapper_[r].update(s_[r], curChannel, out);
    }
    for (const thgCell &c : cells)
    {
        thhState &s = s_[c.row];

        s.axis[THG_LIFE_ROW] = c.row / (float)(thgLife::ROWS - 1);
        s.axis[THG_LIFE_COLUMN] = c.col / (float)(thgLife::COLS - 1);
        s.axis[THG_LIFE_NEIGHBORS] = c.neighbors / 8.0f;
        s.buttons = 1u << THG_LIFE_CELL | (c.born ? 1u << THG_LIFE_BORN : 0);
        s.time = time + 1e-4;
        mapper_[c.row].update(s, curChannel, out);
    }
}

void
thgLifePlayer::release (std::vector<thhAction> &out)
{
    for (int r = 0; r < thgLife::ROWS; r++)
    {
        mapper_[r].release(out);
        s_[r].buttons = 0;
    }
}
