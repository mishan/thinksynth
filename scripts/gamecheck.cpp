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
 * gamecheck -- marbles and pegs (game/thgMarbles.h): the physics against
 * what physics says, the taps and shakes, and the start the seed gives.
 */

#include <cmath>
#include <cstdio>
#include <string>

#include "thcHidBlock.h"
#include "thgMarbles.h"
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

/* A world of one ball at (x, y) moving (vx, vy), and pegs. */
static thgMarbles
world (float x, float y, float vx, float vy, std::vector<thgPeg> pegs)
{
    thgMarbles m(320, 240, 1);

    for (const thgPeg &p : pegs)
        m.tap(p.x, p.y);
    m.shake();
    const_cast<thgBall &>(m.balls()[0]) = { x, y, vx, vy };
    return m;
}

int
main ()
{
    const float dt = 0.004f, g = 600;
    char why[200];

    /* Dropped from rest onto a peg below: the first hit is the peg, as
       fast as falling the gap between them makes it. */
    {
        thgMarbles m = world(160, 50, 0, 0, { { 160, 150 } });
        std::vector<thgHit> hits;
        const float gap = 150 - 50 - (thgMarbles::BALL_R + thgMarbles::PEG_R);
        const float want = std::sqrt(2 * g * gap);

        for (int i = 0; i < 250 && hits.empty(); i++)
            m.step(0, g, dt, hits);
        snprintf(why, sizeof(why), "%zu hits, the first %s at %.0f px/s, wanted the peg at %.0f",
                 hits.size(), hits.empty() ? "none" : hits[0].peg == 0 ? "the peg" : "a wall",
                 hits.empty() ? 0 : hits[0].speed, want);
        report("a ball dropped on a peg hits it as fast as it fell",
               hits.size() == 1 && hits[0].peg == 0 && std::fabs(hits[0].speed - want) < 0.05f * want,
               why);
    }

    /* Dropped to the floor: each bounce slower, until it rests, after
       which the floor isn't hit again. */
    {
        thgMarbles m = world(160, 100, 0, 0, {});
        std::vector<thgHit> hits;
        bool slower = true;

        for (int i = 0; i < 1500; i++)
            m.step(0, g, dt, hits);
        for (size_t i = 1; i < hits.size(); i++)
            slower = slower && hits[i].speed < hits[i - 1].speed;

        const size_t settled = hits.size();

        for (int i = 0; i < 500; i++)
            m.step(0, g, dt, hits);
        snprintf(why, sizeof(why), "%zu bounces, %s slower each time; %zu more resting",
                 settled, slower ? "each" : "not", hits.size() - settled);
        report("a ball bounces lower each time, then rests without hitting",
               settled > 1 && settled < 20 && slower && hits.size() == settled &&
               hits[0].peg == -1, why);
    }

    /* Tipped left, it rolls left. */
    {
        thgMarbles m = world(160, 234, 0, 0, {});
        std::vector<thgHit> hits;

        for (int i = 0; i < 100; i++)
            m.step(-g, g * 0.2f, dt, hits);
        snprintf(why, sizeof(why), "at %.1f", m.balls()[0].x);
        report("tipped left, it rolls left", m.balls()[0].x < 140, why);
    }

    /* Fast enough to cross a peg in one step, and it still strikes it. */
    {
        /* 40 px a step: past a peg and a ball together, 26 across. */
        thgMarbles m = world(20, 120, 10000, 0, { { 160, 120 } });
        std::vector<thgHit> hits;

        for (int i = 0; i < 20; i++)
            m.step(0, 0, dt, hits);
        report("a ball can't pass through a peg however fast",
               !hits.empty() && hits[0].peg == 0, hits.empty() ? "no hit" : "a wall first");
    }

    /* A tap puts a peg; a tap on it takes it away; no more than PEGS. */
    {
        thgMarbles m(320, 240, 1);

        m.tap(100, 100);
        const size_t one = m.pegs().size();
        m.tap(103, 98);
        const size_t none = m.pegs().size();

        for (int i = 0; i < 40; i++)
            m.tap(20 + (i % 8) * 38, 30 + (i / 8) * 40);
        snprintf(why, sizeof(why), "%zu, %zu, %zu", one, none, m.pegs().size());
        report("a tap puts a peg, a tap on it takes it away, up to the most",
               one == 1 && none == 0 && m.pegs().size() == (size_t)thgMarbles::PEGS, why);
    }

    /* A tap off the field puts no peg there. */
    {
        thgMarbles m(320, 240, 1);

        for (const thgPeg &p : std::vector<thgPeg>{ { 100, -5 }, { -3, 100 }, { 330, 100 },
                                                   { 100, 250 } })
            m.tap(p.x, p.y);
        report("a tap off the field puts no peg", m.pegs().empty(),
               std::to_string(m.pegs().size()));
    }

    /* Into a corner, it strikes both walls, not one. */
    {
        thgMarbles m = world(20, 20, -500, -500, {});
        std::vector<thgHit> hits;

        for (int i = 0; i < 10; i++)
            m.step(0, 0, dt, hits);
        snprintf(why, sizeof(why), "%zu hits", hits.size());
        report("a ball into a corner strikes both walls", hits.size() == 2 &&
               hits[0].peg == -1 && hits[1].peg == -1, why);
    }

    /* A shake drops a ball in, up to BALLS. */
    {
        thgMarbles m(320, 240, 1);

        for (int i = 0; i < 6; i++)
            m.shake();
        report("a shake drops a ball in, up to the most",
               m.balls().size() == (size_t)thgMarbles::BALLS, std::to_string(m.balls().size()));
    }

    /* The seed decides the scatter, and no two pegs touch. */
    {
        thgMarbles a(320, 240, 7), b(320, 240, 7), c(320, 240, 8);
        bool apart = true, same = true, differ = false;

        a.scatter(8);
        b.scatter(8);
        c.scatter(8);
        for (size_t i = 0; i < a.pegs().size(); i++)
        {
            same = same && a.pegs()[i].x == b.pegs()[i].x && a.pegs()[i].y == b.pegs()[i].y;
            differ = differ || a.pegs()[i].x != c.pegs()[i].x;
            for (size_t j = 0; j < i; j++)
                apart = apart && std::hypot(a.pegs()[i].x - a.pegs()[j].x,
                                            a.pegs()[i].y - a.pegs()[j].y) >
                                 2 * thgMarbles::PEG_R + 2 * thgMarbles::BALL_R;
        }
        report("the seed decides the scatter, and pegs leave a ball room between",
               a.pegs().size() == 8 && same && differ && apart, "");
    }

    /* The game's own profile: a peg mid-screen is C5, harder louder; a
       wall is a note on channel 2, for a percussion patch there. */
    {
        thcHidBlock p;
        thhMapper m;
        thhState st;
        std::vector<thhAction> acts;
        std::string err;
        int line;
        bool ok = thcHidParseProfile(THG_MARBLES_PROFILE, p, err, line) &&
                  m.configure(p, thgMarblesControls(), acts, err, line);
        int peg = 0, wall = 0;

        st.connected = true;
        st.axis[THG_X] = 0.5f;
        st.axis[THG_SPEED] = 1;
        m.update(st, 0, acts);
        for (int which : { THG_HIT, THG_WALL })
        {
            acts.clear();
            st.buttons = 1u << which;
            st.time += 0.01;
            m.update(st, 0, acts);
            for (const thhAction &a : acts)
                if (a.kind == thhAction::NOTE_ON && a.vel == 127)
                    (which == THG_HIT ? peg : wall) = a.channel == 0 && a.note == 72 ? 1
                                                    : a.channel == 1 ? 2 : -1;
            st.buttons = 0;
            st.time += 0.01;
            m.update(st, 0, acts);
        }
        report("the game's profile: a peg mid-screen C5, a wall on channel 2",
               ok && peg == 1 && wall == 2, ok ? "" : err);
    }

    printf("%d failure%s\n", failed, failed == 1 ? "" : "s");
    return failed ? 1 : 0;
}
