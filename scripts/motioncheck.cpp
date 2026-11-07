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
 * motioncheck -- what thMotion makes of a picture, drawn rather than filmed.
 *
 * Every frame here is a flat background with bright squares on it, so where
 * a gesture is and how fast it moves are known exactly.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <functional>
#include <string>
#include <vector>

#include "thMotion.h"

using std::string;

static const int W = 160, H = 120;
static const uint8_t BACKGROUND = 40, HAND = 220, SIDE = 16;

static int failed = 0;

static void check (const char *name, bool ok, const string &why = "")
{
    if (ok)
        printf("ok    %s\n", name);
    else
    {
        printf("FAIL  %s\n  %s\n", name, why.c_str());
        failed++;
    }
}

static string fmt (const char *f, double a, double b = 0, double c = 0)
{
    char s[256];
    snprintf(s, sizeof s, f, a, b, c);
    return s;
}

struct Frame {
    int width, height, stride;
    std::vector<uint8_t> px;

    Frame (int w = W, int h = H, int s = W)
        : width(w), height(h), stride(s), px((size_t)s * h) { }

    void clear (uint8_t level)
    {
        for (int y = 0; y < height; y++)
            memset(&px[(size_t)y * stride], level, width);
    }

    /* A square centered on (cx, cy) in the picture's own coordinates. */
    void square (double cx, double cy)
    {
        for (int y = (int)lround(cy - SIDE / 2.0); y < lround(cy + SIDE / 2.0); y++)
            for (int x = (int)lround(cx - SIDE / 2.0); x < lround(cx + SIDE / 2.0); x++)
                if (x >= 0 && x < width && y >= 0 && y < height)
                    px[(size_t)y * stride + x] = HAND;
    }
};

/* Feeds `seconds' of frames at `fps' and returns the mean of `axis' over
   them, every frame drawn by `draw' at its own time. */
static double play (thMotion &m, double fps, double seconds,
                    const std::function<void (Frame &, double)> &draw,
                    thMotion::Axis axis = thMotion::MOTION,
                    Frame frame = Frame())
{
    const int n = (int)lround(fps * seconds);
    double sum = 0;

    for (int i = 0; i < n; i++)
    {
        const double t = i / fps;

        draw(frame, t);
        m.feed(frame.px.data(), frame.stride, t);
        sum += m.axis(axis);
    }

    return sum / n;
}

static thMotion::Axis loudestZone (const thMotion &m)
{
    int best = thMotion::ZONE1;

    for (int z = thMotion::ZONE1; z <= thMotion::ZONE9; z++)
        if (m.axis((thMotion::Axis)z) > m.axis((thMotion::Axis)best))
            best = z;

    return (thMotion::Axis)best;
}

/* A square trembling in place: moving, so it is seen, but going nowhere. */
static std::function<void (Frame &, double)> tremble (double cx, double cy)
{
    return [cx, cy] (Frame &f, double t) {
        f.clear(BACKGROUND);
        f.square(cx + 3 * sin(t * 40), cy);
    };
}

static void stillIsStill (void)
{
    /* Under the noise floor, every pixel, every frame. */
    for (int noise : { 0, thMotion::NOISE_LEVELS / 2 })
    {
        thMotion m(W, H);
        unsigned seed = 1;
        double peak = 0;

        play(m, 30, 1, [&] (Frame &f, double) {
            for (auto &p : f.px)
            {
                seed = seed * 1103515245 + 12345;
                p = (uint8_t)(BACKGROUND + (int)((seed >> 16) % (2 * noise + 1)) - noise);
            }
        });

        for (int a = 0; a < thMotion::AXES; a++)
            if (a == thMotion::MOTION || a >= thMotion::ZONE1)
                peak = fmax(peak, m.axis((thMotion::Axis)a));

        check(noise ? "sensor noise is not motion" : "a still picture is not motion",
              peak == 0, fmt("motion or a zone at %.3f", peak));
    }
}

static void whereThePlayerSeesIt (void)
{
    /* Picture coordinates in, the player's view out: the picture's right is
       the player's left. */
    static const struct {
        const char *name;
        double cx, cy, x, y;
        thMotion::Axis zone;
    } rows[] = {
        { "a hand at the player's top left",     140,  15, 0.125, 0.125, thMotion::ZONE1 },
        { "a hand in the middle",                 80,  60, 0.5,   0.5,   thMotion::ZONE5 },
        { "a hand at the player's right",         20,  60, 0.875, 0.5,   thMotion::ZONE6 },
        { "a hand at the player's bottom right",  20, 105, 0.875, 0.875, thMotion::ZONE9 },
        { "a hand at the player's bottom left",  140, 105, 0.125, 0.875, thMotion::ZONE7 },
    };

    for (const auto &r : rows)
    {
        thMotion m(W, H);

        play(m, 60, 0.5, tremble(r.cx, r.cy));

        const double x = m.axis(thMotion::X), y = m.axis(thMotion::Y);

        check(r.name,
              fabs(x - r.x) < 0.05 && fabs(y - r.y) < 0.05 && loudestZone(m) == r.zone,
              fmt("x %.3f y %.3f, loudest zone %.0f", x, y,
                  loudestZone(m) - thMotion::ZONE1 + 1));
    }
}

/* A square crossing the picture at `speed' pixels a second. */
static std::function<void (Frame &, double)> sweep (double speed)
{
    return [speed] (Frame &f, double t) {
        f.clear(BACKGROUND);
        f.square(20 + speed * t, 60);
    };
}

static void perSecond (void)
{
    thMotion slowCam(W, H), fastCam(W, H);
    const double at30 = play(slowCam, 30, 0.5, sweep(200));
    const double at120 = play(fastCam, 120, 0.5, sweep(200));

    check("a gesture is as loud at 120 fps as at 30",
          at30 > 0 && at120 / at30 > 0.7 && at120 / at30 < 1.43,
          fmt("motion %.3f at 30 fps, %.3f at 120", at30, at120));

    thMotion slow(W, H), fast(W, H);
    const double gentle = play(slow, 60, 0.3, sweep(100));
    const double brisk = play(fast, 60, 0.3, sweep(400));

    check("a faster gesture is louder",
          brisk > 2 * gentle && brisk < 1,
          fmt("motion %.3f at 100 px/s, %.3f at 400", gentle, brisk));
}

static void stillnessHolds (void)
{
    thMotion m(W, H);
    Frame f;

    play(m, 60, 0.5, tremble(120, 30));

    /* The first still frame still differs from the last moving one. */
    f.clear(BACKGROUND);
    f.square(120, 30);
    m.feed(f.px.data(), f.stride, 0.5);

    const double x = m.axis(thMotion::X), y = m.axis(thMotion::Y);
    const double spread = m.axis(thMotion::SPREAD);

    for (int i = 1; i <= 10; i++)
        m.feed(f.px.data(), f.stride, 0.5 + i / 60.0);

    check("a hand that stops keeps its place",
          fabs(x - 0.25) < 0.05 && m.axis(thMotion::MOTION) == 0 && m.axis(thMotion::ZONE1) == 0
          && m.axis(thMotion::X) == x && m.axis(thMotion::Y) == y
          && m.axis(thMotion::SPREAD) == spread,
          fmt("motion %.3f, x %.3f -> %.3f", m.axis(thMotion::MOTION), x,
              m.axis(thMotion::X)));
}

static void spread (void)
{
    thMotion one(W, H), two(W, H);
    const double alone = play(one, 60, 0.5, tremble(80, 60), thMotion::SPREAD);
    const double apart = play(two, 60, 0.5, [] (Frame &f, double t) {
        f.clear(BACKGROUND);
        f.square(20 + 3 * sin(t * 40), 60);
        f.square(140 + 3 * sin(t * 40), 60);
    }, thMotion::SPREAD);

    check("two hands apart spread wider than one",
          apart > 2 * alone && apart > 0.3,
          fmt("spread %.3f for one, %.3f for two apart", alone, apart));
}

static void brightness (void)
{
    for (int level : { 64, 192 })
    {
        thMotion m(W, H);

        play(m, 30, 0.1, [level] (Frame &f, double) { f.clear((uint8_t)level); });

        const double want = level / 255.0, got = m.axis(thMotion::BRIGHT);

        check(level < 128 ? "a dark picture is dark" : "a bright picture is bright",
              fabs(got - want) < 0.01, fmt("brightness %.3f, wanted %.3f", got, want));
    }
}

static void stalls (void)
{
    /* The square jumps across the picture between the two frames. */
    static const struct { const char *name; double gap; } rows[] = {
        { "a jump across a stall is not a gesture", 1.0 },
        { "a frame stamped no later is not a gesture", 0.0 },
    };

    for (const auto &r : rows)
    {
        thMotion m(W, H);
        Frame f;

        f.clear(BACKGROUND);
        f.square(20, 60);
        m.feed(f.px.data(), f.stride, 10);
        f.clear(BACKGROUND);
        f.square(140, 60);
        m.feed(f.px.data(), f.stride, 10 + r.gap);

        check(r.name, m.axis(thMotion::MOTION) == 0,
              fmt("motion %.3f", m.axis(thMotion::MOTION)));
    }
}

static void padding (void)
{
    /* An odd size in a padded buffer, whose padding changes every frame:
       nothing in it is picture. */
    const int w = 161, h = 121, stride = 176;
    thMotion m(w, h);
    int frame = 0;
    const double motion = play(m, 30, 0.5, [&] (Frame &f, double) {
        memset(f.px.data(), frame++ & 1 ? 255 : 0, f.px.size());
        f.clear(BACKGROUND);
    }, thMotion::MOTION, Frame(w, h, stride));

    check("a buffer's padding is not picture", motion == 0,
          fmt("motion %.3f", motion));

    thMotion corner(w, h);

    play(corner, 60, 0.5, [] (Frame &f, double t) {
        f.clear(BACKGROUND);
        f.square(5 + 3 * sin(t * 40), 115);
    }, thMotion::MOTION, Frame(w, h, stride));

    check("an odd size's last pixels are in the last zone",
          loudestZone(corner) == thMotion::ZONE9 && corner.axis(thMotion::ZONE9) > 0,
          fmt("loudest zone %.0f", loudestZone(corner) - thMotion::ZONE1 + 1));
}

int main (void)
{
    stillIsStill();
    whereThePlayerSeesIt();
    perSecond();
    stillnessHolds();
    spread();
    brightness();
    stalls();
    padding();

    if (failed)
        printf("%d failed\n", failed);

    return failed ? 1 : 0;
}
