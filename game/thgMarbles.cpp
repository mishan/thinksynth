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

#include <algorithm>
#include <cmath>

#include "thgMarbles.h"

/* What a bounce off a peg and off a wall keeps of the speed into it. */
#define THG_PEG_BOUNCE 0.75f
#define THG_WALL_BOUNCE 0.6f
/* The air's share of a ball's speed taken a second. */
#define THG_DRAG 0.1f
/* A ball striking the same thing again this soon is still the first
   strike, rattling. */
#define THG_AGAIN 0.06f

const char *const THG_MARBLES_PROFILE =
    "device marbles {\n"
    "    base = \"C4\";\n"
    "    hit = note 0;\n"
    "    wall = note 0 { channel = 2; };\n"
    "    x = pitch { scale = minor_pentatonic; range = 10; };\n"
    "    speed = velocity { min = 20; max = 127; };\n"
    "};\n";

const thhControls &
thgMarblesControls ()
{
    static const thhControls c = {
        "marbles",
        { "hit", "wall" },
        { { "x", false }, { "y", false }, { "speed", false } },
    };

    return c;
}

thgMarbles::thgMarbles (float w, float h, uint32_t seed)
    : w_(w), h_(h), rand_(seed ? seed : 1), now_(0)
{
}

/* xorshift32, 0 to 1. */
float
thgMarbles::random ()
{
    rand_ ^= rand_ << 13;
    rand_ ^= rand_ >> 17;
    rand_ ^= rand_ << 5;
    return (rand_ >> 8) * (1.0f / 16777216.0f);
}

void
thgMarbles::scatter (int n)
{
    /* Room for a ball between any two, and off the walls by as much. */
    const float apart = 2 * PEG_R + 2 * BALL_R, margin = PEG_R + 2 * BALL_R;

    for (int tries = 0; tries < 1000 && (int)pegs_.size() < n && (int)pegs_.size() < PEGS;
         tries++)
    {
        const thgPeg p = { margin + random() * (w_ - 2 * margin),
                           h_ * 0.25f + random() * (h_ * 0.75f - margin) };
        bool free = true;

        for (const thgPeg &q : pegs_)
            free = free && std::hypot(p.x - q.x, p.y - q.y) > apart;
        if (free)
        {
            pegs_.push_back(p);
            for (std::vector<float> &l : lastPeg_)
                l.push_back(-1);
        }
    }
}

void
thgMarbles::tap (float x, float y)
{
    if (x < 0 || x > w_ || y < 0 || y > h_)
        return;
    for (size_t i = 0; i < pegs_.size(); i++)
        if (std::hypot(pegs_[i].x - x, pegs_[i].y - y) < PEG_R + BALL_R)
        {
            pegs_.erase(pegs_.begin() + i);
            for (std::vector<float> &l : lastPeg_)
                l.erase(l.begin() + i);
            return;
        }

    if ((int)pegs_.size() >= PEGS)
        return;
    for (const thgBall &b : balls_)
        if (std::hypot(b.x - x, b.y - y) < BALL_R + PEG_R)
            return;
    pegs_.push_back({ x, y });
    for (std::vector<float> &l : lastPeg_)
        l.push_back(-1);
}

void
thgMarbles::shake ()
{
    if ((int)balls_.size() >= BALLS)
        return;
    balls_.push_back({ w_ * (0.3f + 0.4f * random()), BALL_R + 1,
                       (random() - 0.5f) * 100, 0 });
    lastPeg_.push_back(std::vector<float>(pegs_.size(), -1));
    lastWall_.push_back({ -1, -1, -1, -1 });
}

void
thgMarbles::step (float gx, float gy, float dt, std::vector<thgHit> &hits)
{
    now_ += dt;
    for (int b = 0; b < (int)balls_.size(); b++)
        move(b, gx, gy, dt, hits);

    /* Balls on each other, as equal masses: the speeds along the line
       between them swapped, and the two put apart. */
    for (size_t i = 0; i < balls_.size(); i++)
        for (size_t j = i + 1; j < balls_.size(); j++)
        {
            thgBall &a = balls_[i], &c = balls_[j];
            const float dx = c.x - a.x, dy = c.y - a.y, d = std::hypot(dx, dy);

            if (d <= 0 || d >= 2 * BALL_R)
                continue;

            const float nx = dx / d, ny = dy / d;
            const float va = a.vx * nx + a.vy * ny, vc = c.vx * nx + c.vy * ny;

            if (va - vc > 0)
            {
                a.vx += (vc - va) * nx;
                a.vy += (vc - va) * ny;
                c.vx += (va - vc) * nx;
                c.vy += (va - vc) * ny;
            }

            const float push = (2 * BALL_R - d) / 2;

            a.x -= nx * push;
            a.y -= ny * push;
            c.x += nx * push;
            c.y += ny * push;
        }
}

void
thgMarbles::move (int b, float gx, float gy, float dt, std::vector<thgHit> &hits)
{
    thgBall &ball = balls_[b];
    /* Steps of no more than half a radius, so none passes through a peg. */
    const float far = std::hypot(ball.vx, ball.vy) * dt + std::hypot(gx, gy) * dt * dt;
    const int n = std::max(1, std::min(64, (int)std::ceil(far / (BALL_R * 0.5f))));
    const float sub = dt / n;
    /* wall: which, 0 to 3, where the peg is -1. */
    const auto struck = [&](int peg, int wall, float into, float x, float y) {
        float &last = peg >= 0 ? lastPeg_[b][peg] : lastWall_[b][wall];

        if (into > HIT_SPEED && (last < 0 || now_ - last > THG_AGAIN))
            hits.push_back({ b, peg, x, y, into });
        if (into > HIT_SPEED)
            last = now_;
    };

    for (int s = 0; s < n; s++)
    {
        ball.vx = (ball.vx + gx * sub) * (1 - THG_DRAG * sub);
        ball.vy = (ball.vy + gy * sub) * (1 - THG_DRAG * sub);
        ball.x += ball.vx * sub;
        ball.y += ball.vy * sub;

        for (int p = 0; p < (int)pegs_.size(); p++)
        {
            const float dx = ball.x - pegs_[p].x, dy = ball.y - pegs_[p].y;
            const float d = std::hypot(dx, dy);

            if (d <= 0 || d >= BALL_R + PEG_R)
                continue;

            const float nx = dx / d, ny = dy / d;
            const float vn = ball.vx * nx + ball.vy * ny;

            if (vn < 0)
            {
                struck(p, 0, -vn, pegs_[p].x, pegs_[p].y);
                ball.vx -= (1 + THG_PEG_BOUNCE) * vn * nx;
                ball.vy -= (1 + THG_PEG_BOUNCE) * vn * ny;
            }
            ball.x = pegs_[p].x + nx * (BALL_R + PEG_R);
            ball.y = pegs_[p].y + ny * (BALL_R + PEG_R);
        }

        /* The walls, each the same: put back inside, the speed into it
           turned round and lessened. */
        if (ball.x < BALL_R)
        {
            ball.x = BALL_R;
            if (ball.vx < 0)
            {
                struck(-1, 0, -ball.vx, 0, ball.y);
                ball.vx *= -THG_WALL_BOUNCE;
            }
        }
        if (ball.x > w_ - BALL_R)
        {
            ball.x = w_ - BALL_R;
            if (ball.vx > 0)
            {
                struck(-1, 1, ball.vx, w_, ball.y);
                ball.vx *= -THG_WALL_BOUNCE;
            }
        }
        if (ball.y < BALL_R)
        {
            ball.y = BALL_R;
            if (ball.vy < 0)
            {
                struck(-1, 2, -ball.vy, ball.x, 0);
                ball.vy *= -THG_WALL_BOUNCE;
            }
        }
        if (ball.y > h_ - BALL_R)
        {
            ball.y = h_ - BALL_R;
            if (ball.vy > 0)
            {
                struck(-1, 3, ball.vy, ball.x, h_);
                ball.vy *= -THG_WALL_BOUNCE;
            }
        }
    }
}
