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
        double peak = play(m, 30, 1, [&] (Frame &f, double) {
            for (auto &p : f.px)
            {
                seed = seed * 1103515245 + 12345;
                p = (uint8_t)(BACKGROUND + (int)((seed >> 16) % (2 * noise + 1)) - noise);
            }
        });

        for (int a = thMotion::ZONE1; a <= thMotion::ZONE9; a++)
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

    /* In the zone it starts in, too: a strike's velocity is read there. */
    thMotion slow(W, H), fast(W, H);
    const double gentle = play(slow, 60, 0.1, sweep(100), thMotion::ZONE6);
    const double brisk = play(fast, 60, 0.1, sweep(400), thMotion::ZONE6);

    check("a faster gesture is louder in its zone",
          brisk > 2 * gentle && brisk < 1,
          fmt("zone %.3f at 100 px/s, %.3f at 400", gentle, brisk));
}

static void stillnessHolds (void)
{
    thMotion m(W, H);
    Frame f;

    play(m, 60, 0.5, tremble(120, 30));

    /* A still frame differs from the moving ones for the comparison's
       thirtieth of a second. */
    f.clear(BACKGROUND);
    f.square(120, 30);
    for (int i = 0; i < 6; i++)
        m.feed(f.px.data(), f.stride, 0.5 + i / 60.0);

    const double x = m.axis(thMotion::X), y = m.axis(thMotion::Y);
    const double spread = m.axis(thMotion::SPREAD);

    for (int i = 6; i <= 16; i++)
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
    /* The square jumps across the picture between the two frames, and the
       frame after is compared with the jump. */
    static const struct { const char *name; double gap; } rows[] = {
        { "a jump across a stall is not a gesture", 1.0 },
        { "a jump stamped earlier is not a gesture", -1.0 },
    };

    for (const auto &r : rows)
    {
        thMotion m(W, H);
        Frame f;

        for (int i = 0; i < 6; i++)
        {
            tremble(20, 60)(f, i / 30.0);
            m.feed(f.px.data(), f.stride, 10 + i / 30.0);
        }
        f.clear(BACKGROUND);
        f.square(140, 60);
        m.feed(f.px.data(), f.stride, 10 + 5 / 30.0 + r.gap);

        const double jump = m.axis(thMotion::MOTION);

        f.clear(BACKGROUND);
        f.square(146, 60);
        m.feed(f.px.data(), f.stride, 10 + 5 / 30.0 + r.gap + 1 / 30.0);

        check(r.name, jump == 0 && m.axis(thMotion::MOTION) > 0,
              fmt("motion %.3f across it, %.3f after", jump, m.axis(thMotion::MOTION)));
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

    /* One column flashing at the picture's first or last column, in the
       bottom row of zones. */
    static const struct { int column; thMotion::Axis zone; } edges[] = {
        { 0, thMotion::ZONE9 }, { w - 1, thMotion::ZONE7 },
    };

    for (const auto &e : edges)
    {
        thMotion edge(w, h);
        int flash = 0;

        play(edge, 30, 0.5, [&] (Frame &f, double) {
            f.clear(BACKGROUND);
            if (flash++ % 2)
                for (int y = 100; y < h; y++)
                    f.px[(size_t)y * stride + e.column] = HAND;
        }, thMotion::MOTION, Frame(w, h, stride));

        check(e.column == 0 ? "an odd size's first column is the player's right"
                            : "an odd size's last column is the player's left",
              loudestZone(edge) == e.zone && edge.axis(e.zone) > 0,
              fmt("loudest zone %.0f", loudestZone(edge) - thMotion::ZONE1 + 1));
    }
}

/* Gaussian, as a sensor's noise is. */
static double gaussian (unsigned &seed)
{
    seed = seed * 1103515245 + 12345;
    const double u = ((seed >> 8) + 1.0) / 16777217.0;
    seed = seed * 1103515245 + 12345;
    const double v = ((seed >> 8) + 1.0) / 16777217.0;

    return sqrt(-2 * log(u)) * cos(2 * M_PI * v);
}

static void noisyStillness (void)
{
    for (double sigma : { 2.0, 5.0 })
    {
        thMotion m(W, H);
        Frame f;
        unsigned seed = 7;
        auto noisy = [&] (double cx, double t) {
            f.clear(BACKGROUND);
            f.square(cx, 30);
            for (auto &p : f.px)
                p = (uint8_t)fmin(255, fmax(0, lround(p + sigma * gaussian(seed))));
            m.feed(f.px.data(), f.stride, t);
        };
        double t = 0;

        for (; t < 0.5; t += 1 / 60.0)
            noisy(120 + 3 * sin(t * 40), t);
        for (int i = 0; i < 6; i++, t += 1 / 60.0)
            noisy(120, t);

        const double x = m.axis(thMotion::X), y = m.axis(thMotion::Y);

        for (int i = 0; i < 60; i++, t += 1 / 60.0)
            noisy(120, t);

        check(sigma < 3 ? "a hand that stops amid a little noise keeps its place"
                        : "a hand that stops amid a lot of noise keeps its place",
              fabs(x - 0.25) < 0.05 && m.axis(thMotion::X) == x && m.axis(thMotion::Y) == y,
              fmt("x %.3f -> %.3f", x, m.axis(thMotion::X)));
    }
}

/* Like sweep, but the square's edges ramp over `soft' pixels, as a hand's
   do once a frame is scaled down. */
static std::function<void (Frame &, double)> softSweep (double speed, double soft)
{
    return [speed, soft] (Frame &f, double t) {
        const double cx = 20 + speed * t, cy = 60, half = SIDE / 2.0;

        for (int y = 0; y < f.height; y++)
            for (int x = 0; x < f.width; x++)
            {
                const double out = fmax(fabs(x + 0.5 - cx), fabs(y + 0.5 - cy)) - half;
                const double in = fmin(1, fmax(0, 0.5 - out / soft));

                f.px[(size_t)y * f.stride + x] = (uint8_t)lround(BACKGROUND + in * (HAND - BACKGROUND));
            }
    };
}

static void slowSoftHand (void)
{
    thMotion slowCam(W, H), fastCam(W, H);
    const double at30 = play(slowCam, 30, 1, softSweep(30, 8));
    const double at120 = play(fastCam, 120, 1, softSweep(30, 8));

    check("a slow, soft-edged hand is seen at 120 fps as at 30",
          at30 > 0 && at120 / at30 > 0.7 && at120 / at30 < 1.43,
          fmt("motion %.4f at 30 fps, %.4f at 120", at30, at120));
}

static void lightIsNotMotion (void)
{
    /* Exposure stepping up and down on a picture with every level in it. */
    for (double gain : { 1.1, 1.25 })
    {
        thMotion m(W, H);
        int frame = 0;
        const double motion = play(m, 30, 1, [&] (Frame &f, double) {
            const double g = frame++ % 4 < 2 ? 1 : gain;

            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++)
                    f.px[(size_t)y * W + x] = (uint8_t)fmin(255, g * (16 + x * 200 / W));
        });

        check(gain < 1.2 ? "exposure stepping a little is not motion"
                         : "exposure stepping a lot is not motion",
              motion == 0, fmt("motion %.3f", motion));
    }

    /* A hand crossing while exposure steps under it: seen on every frame,
       the steps included. */
    thMotion crossing(W, H);
    int step = 0;
    double least = 1;

    play(crossing, 30, 0.5, [&] (Frame &f, double t) {
        const double g = step++ % 4 < 2 ? 1 : 1.25;

        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                f.px[(size_t)y * W + x] = (uint8_t)fmin(255, g * (16 + x * 200 / W));
        f.square(20 + 200 * t, 60);
        if (step > 2)
            least = fmin(least, crossing.axis(thMotion::MOTION));
    });

    check("a hand is seen while exposure steps", least > 0,
          fmt("motion fell to %.3f", least));

    /* A big bright hand coming in from the picture's left, in its top two
       rows of zones: the bottom row must stay quiet. */
    thMotion m(W, H);
    double bottom = 0;

    play(m, 30, 0.5, [&] (Frame &f, double t) {
        f.clear(BACKGROUND);
        for (int y = 5; y < 75; y++)
            for (int x = 0; x < 70; x++)
                if (x < (int)(t * 600) - 60)
                    f.px[(size_t)y * W + x] = HAND;
        bottom = fmax(bottom, fmax(m.axis(thMotion::ZONE7),
                                   fmax(m.axis(thMotion::ZONE8), m.axis(thMotion::ZONE9))));
    });

    check("a big hand coming in moves only its own zones", bottom == 0,
          fmt("the bottom row reached %.3f", bottom));
}

static void repeatedFrames (void)
{
    /* A browser drawing at 60 Hz from a 30 fps camera: every frame twice,
       stamped when drawn or with the camera frame's own time. */
    static const struct { const char *name; bool cameraTime; } rows[] = {
        { "a frame handed over twice does not flicker to stillness", false },
        { "a frame handed over twice with one stamp does not flicker", true },
    };

    for (const auto &r : rows)
    {
        thMotion m(W, H);
        Frame f;
        double least = 1;

        for (int i = 0; i < 30; i++)
        {
            const double camera = (i / 2) / 30.0;

            sweep(200)(f, camera);
            m.feed(f.px.data(), f.stride, r.cameraTime ? camera : i / 60.0);
            if (i >= 6)
                least = fmin(least, m.axis(thMotion::MOTION));
        }

        check(r.name, least > 0, fmt("motion fell to %.3f", least));
    }
}

static void burst (void)
{
    /* Frames stamped a millisecond apart, as a reader stamping buffers when
       it takes them rather than when they were captured would: none is
       compared with another, so a step between them is never divided by a
       millisecond. */
    thMotion m(W, H);
    double most = 0;

    for (int i = 0; i < 20; i++)
    {
        Frame f;

        sweep(2000)(f, i / 1000.0);
        m.feed(f.px.data(), f.stride, 5 + i / 1000.0);
        most = fmax(most, m.axis(thMotion::MOTION));
    }

    check("frames stamped a millisecond apart are not compared", most < 0.1, fmt("motion reached %.3f", most));
}

static void tooSmall (void)
{
    thMotion m(2, 2);
    bool inRange = true;

    for (int i = 0; i < 10; i++)
    {
        const uint8_t px[4] = { (uint8_t)(i * 50), 0, 255, (uint8_t)(i * 20) };

        m.feed(px, 2, i / 30.0);
        for (int a = 0; a < thMotion::AXES; a++)
        {
            const float v = m.axis((thMotion::Axis)a);

            inRange = inRange && v >= 0 && v <= 1 && (a < thMotion::ZONE1 ? true : v == 0);
        }
    }

    check("a frame too small for zones plays nothing", inRange && m.axis(thMotion::MOTION) == 0);
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
    noisyStillness();
    slowSoftHand();
    lightIsNotMotion();
    repeatedFrames();
    burst();
    tooSmall();

    if (failed)
        printf("%d failed\n", failed);

    return failed ? 1 : 0;
}
