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
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * visualcheck -- will a visual plugin survive what the tap will actually hand
 *                it, and does it draw the same thing twice?
 *
 * Every visual module in the plugin directory is loaded, opened, fed a battery
 * of pathological input and drawn to an image surface at a range of sizes. No
 * display, no toolkit, no X: cairo's image backend is enough, which is the
 * practical reason the ABI takes a cairo_t rather than a widget.
 *
 * The inputs are not invented. Each one is something the corpus produces:
 *
 *   silence            a probe on a node this note never drives -- 142 of the
 *                      2650 ports dspprobe measures are exactly this
 *   DC                 a constant arg, which is what a length-1 arg reads as
 *   full scale         a square at +-1
 *   1e5                dsp/old/test.dsp peaks at 1.75e5 (REVIVAL.md §6)
 *   -inf and NaN       mixer.out on dsp/noargs/bd1.dsp reaches -inf in seven
 *                      windows; four shipped DSPs have diverging filters
 *   one sample         the smallest thing a feed can be
 *
 * The NaN case is the one worth having. Every comparison against a NaN is
 * false, so peak tracking written the obvious way silently ignores it and the
 * display reads a confident zero for a signal that has blown up -- which is
 * precisely when someone has a meter open.
 *
 * Determinism is checked because a visualizer is drawn thirty times a second
 * and a frame that differs from its predecessor for no reason is a frame that
 * will flicker. Two instances given the same feed must produce the same
 * pixels.
 *
 * Confirmed to fail before it was trusted to pass:
 *
 *   decaying the peak in draw() rather than feed()  4 of 39 checks
 *   clearing the RMS at the top of feed()           5 of 39
 *
 * The second is the one worth having. The tap publishes one window at a time
 * but the GUI drains however many are waiting, so the same audio arrives split
 * differently on every frame -- a module that treats a feed boundary as
 * meaningful looks fine in isolation and jitters in use.
 *
 * What this does NOT prove: that a module's numbers are right. It draws a
 * meter fed 1e5 and asserts it drew something, not that it said +100 dB.
 * Removing meter's guard against a sample rate of zero, for instance, passes
 * here -- the decay constants go to zero and the display is wrong but
 * perfectly deterministic and crash-free. Correctness of a visualizer is a
 * matter of looking at it.
 *
 *   ./build/scripts/visualcheck -p build/plugins/
 */

#include "config.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include <cairo.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <dirent.h>
#endif

#include "thVisual.h"

using std::string;
using std::vector;

namespace {

int failures = 0;
int checks = 0;

void ok (bool cond, const char *fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 2, 3)))
#endif
;

void ok (bool cond, const char *fmt, ...)
{
    checks++;

    if (cond)
        return;

    va_list ap;

    va_start(ap, fmt);
    printf("FAIL  ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);

    failures++;
}

/* ---- the inputs ---- */

struct Feed {
    const char *what;
    vector<float> samples;
};

void makeFeeds (vector<Feed> &out, unsigned int n)
{
    Feed f;

    f.what = "silence";
    f.samples.assign(n, 0.0f);
    out.push_back(f);

    f.what = "DC at 0.5";
    f.samples.assign(n, 0.5f);
    out.push_back(f);

    f.what = "a sine at full scale";
    f.samples.resize(n);
    for (unsigned int i = 0; i < n; i++)
        f.samples[i] = (float)sin(2.0 * M_PI * (double)i / 64.0);
    out.push_back(f);

    f.what = "a square at +-1";
    f.samples.resize(n);
    for (unsigned int i = 0; i < n; i++)
        f.samples[i] = (i % 32 < 16) ? 1.0f : -1.0f;
    out.push_back(f);

    f.what = "a diverging filter (1e5)";
    f.samples.resize(n);
    for (unsigned int i = 0; i < n; i++)
        f.samples[i] = 1.75e5f * (float)((i % 2) ? 1 : -1);
    out.push_back(f);

    f.what = "negative infinity";
    f.samples.assign(n, -HUGE_VALF);
    out.push_back(f);

    f.what = "NaN";
    f.samples.assign(n, (float)NAN);
    out.push_back(f);

    f.what = "one NaN in a sine";
    f.samples.resize(n);
    for (unsigned int i = 0; i < n; i++)
        f.samples[i] = (float)sin(2.0 * M_PI * (double)i / 64.0);
    if (n > 3)
        f.samples[3] = (float)NAN;
    out.push_back(f);

    f.what = "denormals";
    f.samples.assign(n, 1e-40f);
    out.push_back(f);

    f.what = "a single sample";
    f.samples.assign(1, 0.25f);
    out.push_back(f);
}

/* ---- drawing ---- */

struct Sizes { int w, h; };

const Sizes SIZES[] = {
    { 128, 24 },    /* what a meter asks for                  */
    { 128, 64 },    /* the default probe panel                */
    { 512, 256 },   /* enlarged                               */
    { 1, 1 },       /* a collapsed panel                      */
    { 3, 40 },      /* absurdly narrow                        */
    { 400, 2 },     /* absurdly short                         */
};

const int NUM_SIZES = (int)(sizeof(SIZES) / sizeof(SIZES[0]));

/* Draws into a fresh surface and hands back its pixels. */
bool render (thVisual &visual, void *inst, int w, int h, vector<unsigned char> &px)
{
    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                       w, h);

    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS)
    {
        cairo_surface_destroy(surf);
        return false;
    }

    cairo_t *cr = cairo_create(surf);

    visual.draw(inst, cr, w, h);

    /* A plugin that left the context in an error state -- an unbalanced
       restore, a bad matrix -- would otherwise show up much later as a blank
       canvas with no explanation. */
    const cairo_status_t status = cairo_status(cr);

    cairo_destroy(cr);
    cairo_surface_flush(surf);

    const int stride = cairo_image_surface_get_stride(surf);
    const unsigned char *data = cairo_image_surface_get_data(surf);

    px.assign(data, data + (size_t)stride * h);

    cairo_surface_destroy(surf);

    return status == CAIRO_STATUS_SUCCESS;
}

bool anyInk (const vector<unsigned char> &px)
{
    for (size_t i = 0; i < px.size(); i++)
        if (px[i] != 0)
            return true;

    return false;
}

/* ---- finding the modules ---- */

void listVisuals (const string &dir, vector<string> &out)
{
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    const string pattern = dir + "*";
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);

    if (h == INVALID_HANDLE_VALUE)
        return;

    do {
        const string name = fd.cFileName;

        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".dll") == 0)
            out.push_back(dir + name);
    } while (FindNextFileA(h, &fd));

    FindClose(h);
#else
    DIR *d = opendir(dir.c_str());

    if (d == NULL)
        return;

    struct dirent *e;

    while ((e = readdir(d)) != NULL)
    {
        const string name = e->d_name;

        if (name.size() > 3 && name.compare(name.size() - 3, 3, ".so") == 0)
            out.push_back(dir + name);
    }

    closedir(d);
#endif
}

/* ---- one module ---- */

void checkOne (const string &path)
{
    thVisual visual(path);

    ok(visual.state() == thVisual::LOADED, "%s loads", path.c_str());

    if (visual.state() != thVisual::LOADED)
        return;

    printf("  %-10s %s  (%dx%d)\n", visual.name().c_str(),
           visual.desc().c_str(), visual.preferredWidth(),
           visual.preferredHeight());

    ok(!visual.desc().empty(), "%s describes itself", visual.name().c_str());
    ok(visual.preferredWidth() > 0 && visual.preferredHeight() > 0,
       "%s asks for a positive size", visual.name().c_str());

    /* A sample rate of zero is not a thing the host should pass, and is
       exactly the sort of thing that turns into a division by zero the first
       time a probe is armed before the audio backend has reported its rate. */
    void *zeroRate = visual.open(0);

    ok(zeroRate != NULL, "%s opens even at a sample rate of 0",
       visual.name().c_str());

    if (zeroRate)
    {
        vector<float> s(64, 0.5f);
        vector<unsigned char> px;

        visual.feed(zeroRate, &s[0], (unsigned int)s.size());
        render(visual, zeroRate, 128, 24, px);
        visual.close(zeroRate);
    }

    /* Draw before any feed at all: a panel is on screen the moment it is
       armed, before a single window has been published. */
    {
        void *inst = visual.open(44100);
        vector<unsigned char> px;

        ok(inst != NULL, "%s opens", visual.name().c_str());

        if (inst)
        {
            ok(render(visual, inst, 128, 24, px),
               "%s draws before it has been fed anything",
               visual.name().c_str());

            /* Not merely "did not crash": a panel that drew nothing at all
               would read as a hole in the canvas. */
            ok(anyInk(px), "%s draws something rather than nothing",
               visual.name().c_str());

            visual.close(inst);
        }
    }

    /* Feeding nothing, in the several ways that can happen. */
    {
        void *inst = visual.open(44100);

        if (inst)
        {
            float one = 0.0f;

            visual.feed(inst, NULL, 64);
            visual.feed(inst, &one, 0);

            vector<unsigned char> px;

            ok(render(visual, inst, 128, 24, px),
               "%s survives a NULL and a zero-length feed",
               visual.name().c_str());

            visual.close(inst);
        }
    }

    vector<Feed> feeds;

    makeFeeds(feeds, 1024);

    for (size_t f = 0; f < feeds.size(); f++)
    {
        void *a = visual.open(44100);
        void *b = visual.open(44100);

        if (a == NULL || b == NULL)
        {
            ok(false, "%s could not open two instances", visual.name().c_str());
            visual.close(a);
            visual.close(b);
            continue;
        }

        const float *s = &feeds[f].samples[0];
        const unsigned int n = (unsigned int)feeds[f].samples.size();

        /* One instance gets it in a lump, the other in pieces. A visualizer
           must not care where the window boundaries fell -- the tap publishes
           one window at a time but the GUI drains as many as are waiting, so
           the same audio arrives differently split on every frame. */
        visual.feed(a, s, n);

        for (unsigned int at = 0; at < n; at += 37)
        {
            const unsigned int take = (n - at < 37) ? n - at : 37;

            visual.feed(b, s + at, take);
        }

        bool drewEverywhere = true, sameEverywhere = true;

        for (int z = 0; z < NUM_SIZES; z++)
        {
            vector<unsigned char> pa, pb;

            if (!render(visual, a, SIZES[z].w, SIZES[z].h, pa))
                drewEverywhere = false;

            if (!render(visual, b, SIZES[z].w, SIZES[z].h, pb))
                drewEverywhere = false;

            if (pa != pb)
                sameEverywhere = false;
        }

        ok(drewEverywhere, "%s draws %s at every size without a cairo error",
           visual.name().c_str(), feeds[f].what);

        ok(sameEverywhere,
           "%s draws %s the same however the feed was split",
           visual.name().c_str(), feeds[f].what);

        /* Drawing twice from one instance must not move either. A meter that
           decayed on draw rather than on feed would fail this, and would
           flicker on screen for the same reason. */
        vector<unsigned char> once, twice;

        render(visual, a, 128, 24, once);
        render(visual, a, 128, 24, twice);

        ok(once == twice, "%s draws %s identically twice in a row",
           visual.name().c_str(), feeds[f].what);

        visual.close(a);
        visual.close(b);
    }

    /* Closing NULL, and closing twice is not offered: the host owns the
       pointer and never hands one back after closing it. NULL is worth
       covering because thVisual::close is called on whatever open() returned,
       and open() may return NULL. */
    visual.close(NULL);
    visual.feed(NULL, NULL, 0);
    visual.draw(NULL, NULL, 0, 0);

    ok(true, "%s survives NULL instances at every entry point",
       visual.name().c_str());
}

} /* namespace */

int main (int argc, char **argv)
{
    string pluginPath = PLUGIN_PATH;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-p")) { if (++i >= argc) return 2; pluginPath = argv[i]; }
        else
        {
            printf("usage: %s [-p PLUGIN_PATH]\n", argv[0]);
            return 2;
        }
    }

    if (pluginPath.empty() || pluginPath[pluginPath.size() - 1] != '/')
        pluginPath += '/';

    const string dir = pluginPath + "visual/";

    vector<string> modules;

    listVisuals(dir, modules);

    if (modules.empty())
    {
        printf("FAIL  no visual modules in %s\n", dir.c_str());
        return 1;
    }

    printf("%d visual module(s) in %s\n", (int)modules.size(), dir.c_str());

    for (size_t i = 0; i < modules.size(); i++)
        checkOne(modules[i]);

    printf("\n%d checks, %d failed\n", checks, failures);

    return failures;
}
