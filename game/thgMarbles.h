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

#ifndef THG_MARBLES_H
#define THG_MARBLES_H

#include <stdint.h>

#include <array>
#include <vector>

#include "thhControls.h"

/* Marbles and pegs: balls rolling under the gravity a hand tips the screen
 * to, played by what they hit. No drawing and no sound in it: gravity and
 * touches in, hits out, as controls a profile maps (thgMarblesControls),
 * so a patch's `controller marbles' block says how it plays. Units are
 * pixels and seconds; the world is a w by h screen with walls at its
 * edges. Deterministic for the same seed and inputs. */

struct thgPeg
{
    float x, y;
};

struct thgBall
{
    float x, y, vx, vy;
};

/* A ball striking something: a peg, or a wall where the peg is -1. */
struct thgHit
{
    int   ball, peg;
    float x, y;                 /* where, in pixels */
    float speed;                /* into what it hit, pixels a second */
};

class thgMarbles
{
public:
    static const int BALLS = 4, PEGS = 24;
    static constexpr float BALL_R = 6, PEG_R = 7;
    /* Slower into a peg or a wall than this is resting on it, not a hit. */
    static constexpr float HIT_SPEED = 40;

    thgMarbles (float w, float h, uint32_t seed);

    /* n pegs where none overlaps another, from the seed. */
    void scatter (int n);

    /* A tap: a peg there taken away, else one put there, if there's room
       and no ball in the way; off the field, nothing. */
    void tap (float x, float y);

    /* A ball dropped in at the top, if there's room for one more. */
    void shake ();

    /* dt seconds under gravity (gx, gy), pixels a second squared, the
       hits on the way added to hits. */
    void step (float gx, float gy, float dt, std::vector<thgHit> &hits);

    const std::vector<thgPeg> &pegs () const { return pegs_; }
    const std::vector<thgBall> &balls () const { return balls_; }
    float width () const { return w_; }
    float height () const { return h_; }

private:
    float w_, h_;
    uint32_t rand_;
    std::vector<thgPeg> pegs_;
    std::vector<thgBall> balls_;
    /* When each ball last struck each peg, and each of the four walls: a
       ball rolling along one touches it every step. */
    std::vector<std::vector<float> > lastPeg_;
    std::vector<std::array<float, 4> > lastWall_;
    float now_;

    float random ();
    void move (int b, float gx, float gy, float dt, std::vector<thgHit> &hits);
};

/* The game as a controller, a ball at a time, the way a finger is one:
   `hit' and `wall' pulse down when that ball strikes a peg or a wall,
   `x' and `y' say where (0..1 across and down), `speed' how hard (0..1). */
enum { THG_HIT, THG_WALL };
enum { THG_X, THG_Y, THG_SPEED };

const thhControls &thgMarblesControls ();

/* What marbles play with no block of a patch's: a peg's column a degree
   of C minor pentatonic, two octaves across, harder louder, and a wall a
   note on channel 2, for a percussion patch there. */
extern const char *const THG_MARBLES_PROFILE;

#endif /* THG_MARBLES_H */
