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
 * samplehitscheck -- does osc::sample find the hits in a recording?
 *
 * plugins/osc/samplehits.h, on recordings made here with their hits at
 * known frames: each found where its attack begins and nothing else found,
 * and none in sounds that only swell, tremble or bend. Then the principles,
 * over seeded random recordings: cuts in order, inside the file, apart;
 * more sensitivity never takes one away; `slices' takes the strongest.
 * Last, the shipped break, against the cuts it has always had, which is
 * what says every build -- desktop, browser, each compiler -- cuts the
 * same.
 *
 *   samplehitscheck DSPDIR
 */

#include "config.h"

#include <math.h>
#include <stdio.h>

#include <string>
#include <vector>

#include "think.h"
#include "../plugins/osc/sampleslot.h"
#include "../plugins/osc/samplehits.h"

static int failures = 0;

static void check (bool ok, const std::string &what)
{
    if (!ok)
    {
        fprintf(stderr, "FAIL %s\n", what.c_str());
        failures++;
    }
}

/* A seeded generator, the same on every platform. */
struct Lcg
{
    uint32_t s;

    double next (void)          /* -1..1 */
    {
        s = s * 1664525u + 1013904223u;
        return (double)(s >> 8) / (double)(1u << 23) - 1.0;
    }

    double unit (void) { return (next() + 1) / 2; }
};

/* `n' frames of noise at `level', the floor every recording here has. */
static std::vector<float> room (size_t n, double level, uint32_t seed)
{
    Lcg r = { seed };
    std::vector<float> x(n);

    for (float &v : x)
        v = (float)(level * r.next());

    return x;
}

/* A noise burst at `at': `amp' at its first frame, decaying with `tau'. */
static void burst (std::vector<float> &x, size_t at, double amp, double tau,
                   unsigned rate, uint32_t seed)
{
    Lcg r = { seed };

    for (size_t i = at; i < x.size() && i < at + (size_t)(tau * 6 * rate); i++)
    {
        const double t = (double)(i - at) / rate;

        x[i] += (float)(amp * exp(-t / tau) * r.next());
    }
}

/* A sine from `at', at `hz', `amp' a function of the time since `at'. */
template <class Amp>
static void tone (std::vector<float> &x, size_t at, size_t to, double hz,
                  unsigned rate, Amp amp)
{
    for (size_t i = at; i < x.size() && i < to; i++)
    {
        const double t = (double)(i - at) / rate;

        x[i] += (float)(amp(t) * sin(2 * M_PI * hz * t));
    }
}

static std::string said (const thSampleHits &h)
{
    std::string s;

    for (const thSampleHit &hit : h.hits)
        s += std::to_string(hit.frame) + "(" +
             std::to_string(hit.strength).substr(0, 4) + ") ";

    return s + (h.leadIn ? "lead-in" : "");
}

/* Planted recordings: where each hit's attack begins, and nothing else
   used at the default sensitivity. */
static void planted (void)
{
    const unsigned R = 44100;
    const double sens = 0.5;
    struct Case
    {
        const char *what;
        std::vector<float> x;
        std::vector<size_t> at;
    };
    std::vector<Case> cases;

    {
        Case c = { "bursts, uneven", room(R * 2, 0.003, 1), {} };

        for (size_t at : { 4410, 15000, 22100, 40000, 61000 })
        {
            burst(c.x, at, 0.6, 0.03, R, (uint32_t)at);
            c.at.push_back(at);
        }

        cases.push_back(c);
    }
    {
        /* Each on the last one's decay, as in a groove. */
        Case c = { "a groove", room(R * 2, 0.003, 2), {} };

        for (size_t k = 0; k < 12; k++)
        {
            const size_t at = 2000 + k * (size_t)(0.12 * R);

            burst(c.x, at, k % 4 == 0 ? 0.8 : 0.4, 0.06, R, (uint32_t)k + 9);
            c.at.push_back(at);
        }

        cases.push_back(c);
    }
    {
        /* Struck again before it has died, no gap between. */
        Case c = { "a tone re-struck", room(R * 2, 0.003, 3), {} };

        for (size_t k = 0; k < 4; k++)
        {
            const size_t at = 3000 + k * (size_t)(0.4 * R);

            tone(c.x, at, at + (size_t)(0.4 * R), 220, R,
                 [] (double t) { return 0.6 * exp(-t / 0.3); });
            c.at.push_back(at);
        }

        cases.push_back(c);
    }
    {
        Case c = { "a hit at frame 0", room(R, 0.003, 4), { 0, 20000 } };

        burst(c.x, 0, 0.7, 0.05, R, 11);
        burst(c.x, 20000, 0.7, 0.05, R, 12);
        cases.push_back(c);
    }

    /* Sounds with no hit in them: each swells in over 200 ms. */
    {
        Case c = { "a held tone", room(R * 2, 0.003, 5), {} };

        tone(c.x, 0, R * 2, 220, R,
             [] (double t) { return 0.5 * fmin(1, t / 0.2); });
        cases.push_back(c);
    }
    {
        Case c = { "a tone with 6 Hz tremolo", room(R * 2, 0.003, 6), {} };

        tone(c.x, 0, R * 2, 220, R, [] (double t)
             { return 0.5 * fmin(1, t / 0.2) *
                      (0.75 + 0.25 * sin(2 * M_PI * 6 * t)); });
        cases.push_back(c);
    }
    {
        Case c = { "a tone with vibrato", room(R * 2, 0.003, 7), {} };

        for (size_t i = 0; i < c.x.size(); i++)
        {
            const double t = (double)i / R;

            c.x[i] += (float)(0.5 * fmin(1, t / 0.2) *
                              sin(2 * M_PI * 220 * t +
                                  6.6 * sin(2 * M_PI * 5 * t)));
        }

        cases.push_back(c);
    }
    cases.push_back({ "silence (a stereo file whose sides cancel)",
                      std::vector<float>(R, 0), {} });

    for (const Case &c : cases)
    {
        thSampleHits h;

        thSampleFindHits(c.x.data(), c.x.size(), R, h);

        std::vector<size_t> used;

        for (const thSampleHit &hit : h.hits)
            if (thSampleHitUsed(hit, sens, 0))
                used.push_back(hit.frame);

        bool ok = used.size() == c.at.size() && !h.leadIn;

        for (size_t i = 0; ok && i < used.size(); i++)
            ok = used[i] + R / 1000 >= c.at[i] &&
                 used[i] <= c.at[i] + R / 10000;

        check(ok, std::string(c.what) + ": " + said(h));
    }

    /* Sound before the first hit starts a slice of its own; quiet before
       it, or the hit too near the start, does not. */
    {
        std::vector<float> x = room(R, 0.003, 8);

        tone(x, 0, 8000, 330, R,
             [] (double t) { return 0.3 * fmin(1, t / 0.05); });
        burst(x, 9000, 0.8, 0.03, R, 13);

        thSampleHits h, quiet;
        std::vector<float> y = room(R, 0.003, 8);

        burst(y, 9000, 0.8, 0.03, R, 13);
        thSampleFindHits(x.data(), x.size(), R, h);
        thSampleFindHits(y.data(), y.size(), R, quiet);
        check(h.leadIn && !quiet.leadIn,
              "sound before the first hit is a lead-in, the room's noise is "
              "not: " + said(h) + " / " + said(quiet));
    }

    /* Two clicks alike from silence, 3 hops apart, rise exactly alike:
       one hit, the earlier, never two closer than the gap. */
    {
        std::vector<float> x(R / 2, 0);
        thSampleHits h;

        x[4410] = 0.8f;
        x[4410 + 3 * R / 200] = 0.8f;
        thSampleFindHits(x.data(), x.size(), R, h);
        check(h.hits.size() == 1 && h.hits[0].frame <= 4410,
              "two clicks that rise alike, too close: the earlier, alone: " +
              said(h));
    }

    /* Too short to measure, or a rate too low for a hop: nothing. */
    {
        std::vector<float> x = room(300, 0.003, 9);
        std::vector<float> y = room(R * 6 / 100, 0.003, 10);
        thSampleHits a, b, c;

        burst(x, 100, 0.8, 0.01, R, 14);
        burst(y, R / 100, 0.8, 0.01, R, 15);
        thSampleFindHits(x.data(), x.size(), R, a);
        thSampleFindHits(y.data(), y.size(), R, b);
        thSampleFindHits(y.data(), y.size(), 150, c);
        check(a.hits.empty() && b.hits.size() == 1 && c.hits.empty(),
              "a file under two hops has no hits, one under 20 has its own, "
              "and a rate under 200 has none: " + said(a) + " / " + said(b) +
              " / " + said(c));
    }
}

/* Over seeded random recordings, whatever is in them. */
static void principles (void)
{
    const unsigned rates[] = { 22050, 44100, 48000 };

    for (uint32_t seed = 1; seed <= 200; seed++)
    {
        Lcg r = { seed * 7919u };
        const unsigned R = rates[seed % 3];
        std::vector<float> x = room((size_t)(R * (0.5 + 2 * r.unit())),
                                    0.01 * r.unit(), seed);
        const int bursts = 1 + (int)(12 * r.unit());

        for (int k = 0; k < bursts; k++)
            burst(x, (size_t)(r.unit() * x.size()), 0.05 + 0.85 * r.unit(),
                  0.01 + 0.3 * r.unit(), R, seed * 31 + k);

        thSampleHits h;

        thSampleFindHits(x.data(), x.size(), R, h);

        const size_t hop = R / 200;
        const std::string at = "seed " + std::to_string(seed) + ": ";
        bool apart = true, ranked = h.hits.size() < (size_t)-1;
        std::vector<bool> seen(h.hits.size(), false);

        for (size_t i = 0; i < h.hits.size(); i++)
        {
            const thSampleHit &a = h.hits[i];

            apart = apart && a.frame < x.size() &&
                    (i == 0 || a.frame >= h.hits[i - 1].frame +
                                          THINK_HITS_APART * hop);
            ranked = ranked && a.rank < h.hits.size() && !seen[a.rank] &&
                     a.strength >= 1.0 / 3 && a.strength < 1;

            if (a.rank < seen.size())
                seen[a.rank] = true;

            for (const thSampleHit &b : h.hits)
                if (b.rank < a.rank)
                    ranked = ranked && (b.strength > a.strength ||
                                        (b.strength == a.strength &&
                                         b.frame < a.frame));
        }

        check(apart, at + "cuts in order, inside the file, at least " +
                     std::to_string(THINK_HITS_APART) + " hops apart: " +
                     said(h));
        check(ranked, at + "ranks are each hit's place by strength: " +
                      said(h));

        /* More sensitivity only adds; `slices' N takes the N strongest of
           those above the line. */
        const double sens[] = { 0, 0.2, 0.4, 0.5, 0.6, 0.67, 1 };

        for (size_t s = 0; s + 1 < sizeof sens / sizeof sens[0]; s++)
            for (const thSampleHit &hit : h.hits)
                check(!thSampleHitUsed(hit, sens[s], 0) ||
                      thSampleHitUsed(hit, sens[s + 1], 0),
                      at + "raising sensitivity takes no cut away");

        for (double s : sens)
        {
            size_t above = 0;

            for (const thSampleHit &hit : h.hits)
                above += thSampleHitUsed(hit, s, 0);

            for (int n = 1; n <= 5; n++)
            {
                size_t used = 0;
                double weakestUsed = 2, strongestLeft = -1;

                for (const thSampleHit &hit : h.hits)
                    if (thSampleHitUsed(hit, s, n))
                    {
                        used++;
                        weakestUsed = fmin(weakestUsed, hit.strength);
                    }
                    else if (thSampleHitUsed(hit, s, 0))
                        strongestLeft = fmax(strongestLeft, hit.strength);

                check(used == std::min<size_t>(n, above) &&
                      weakestUsed >= strongestLeft,
                      at + "slices " + std::to_string(n) +
                      " takes the strongest above the line");
            }
        }
    }
}

/* The line `sensitivity' draws, at it and either side of it. */
static void threshold (void)
{
    const struct { double strength, sens; unsigned rank; int slices;
                   bool used; } cases[] = {
        { 0.5, 0.5, 0, 0, true },       /* on the line: in */
        { 0.49, 0.5, 0, 0, false },
        { 0.75, 0.25, 0, 0, true },
        { 0.74, 0.25, 0, 0, false },
        { 1.0 / 3, 1, 0, 0, true },     /* the weakest there is, at 1 */
        { 0.99, 0, 0, 0, false },       /* 0 asks for none */
        { 0.9, 0.5, 2, 3, true },       /* the third strongest of 3 */
        { 0.9, 0.5, 3, 3, false },      /* the fourth */
    };

    for (const auto &c : cases)
        check(thSampleHitUsed({ 0, c.strength, c.rank }, c.sens, c.slices) ==
                  c.used,
              "strength " + std::to_string(c.strength) + " rank " +
              std::to_string(c.rank) + " at sensitivity " +
              std::to_string(c.sens) + " slices " + std::to_string(c.slices) +
              (c.used ? " is used" : " is not"));
}

/* Worked by hand: the first engine frame at or past each file frame. */
static void frames (void)
{
    const struct { size_t f; unsigned rate, fileRate; size_t want; } cases[] = {
        { 0, 48000, 44100, 0 },
        { 1, 48000, 44100, 2 },         /* 1.088... */
        { 44100, 48000, 44100, 48000 },
        { 100, 44100, 48000, 92 },      /* 91.875 */
        { 96, 44100, 48000, 89 },       /* 88.2 */
        { 77, 44100, 44100, 77 },
        { 77, 44100, 0, 77 },           /* a header with no rate */
    };

    for (const auto &c : cases)
        check(thSampleEngineFrame(c.f, c.rate, c.fileRate) == c.want,
              "file frame " + std::to_string(c.f) + " at " +
              std::to_string(c.fileRate) + " is engine frame " +
              std::to_string(c.want) + " at " + std::to_string(c.rate) +
              ", not " + std::to_string(thSampleEngineFrame(c.f, c.rate,
                                                             c.fileRate)));
}

/* The shipped break: the cuts every build has to agree on. */
static const std::string GOLDEN =
    "0(0.93) 12948(0.85) 19094(0.50) 26256(0.99) 39571(0.91) 45719(0.47) "
    "52882(0.84) 65172(0.92) 72336(0.99) 78487(0.77) 91792(0.99) "
    "105102(0.83) ";

static void shipped (const std::string &dir)
{
    thSampleData data;
    std::string why;

    if (!thWavRead(dir + "/samples/loop_break100.wav", 44100, data, why))
    {
        check(false, "loop_break100.wav reads: " + why);
        return;
    }

    thSampleHits h;

    thSampleFindHits(data.frames.data(), data.frames.size(), 44100, h);

    const std::string got = said(h);

    printf("loop_break100.wav: %s\n", got.c_str());
    check(got == GOLDEN, "loop_break100.wav cuts where it always has:\n  " +
                         got + "\nnot\n  " + GOLDEN);
}

int main (int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: samplehitscheck DSPDIR\n");
        return 2;
    }

    planted();
    principles();
    threshold();
    frames();
    shipped(argv[1]);

    if (failures == 0)
        printf("the hits are where they are, and every build finds them "
               "there\n");

    return failures ? 1 : 0;
}
