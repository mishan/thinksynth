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

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include <cairo.h>

#include "thVisual.h"
#include "fftr.h"

namespace fs = std::filesystem;

using std::string;
using std::vector;

/* Picked up automatically by LeakSanitizer, the same way dspstress supplies
 * __tsan_default_options -- so the check stays a real gate instead of needing
 * an environment variable nobody will remember, and CI's ASAN_OPTIONS of
 * detect_leaks=1 keeps meaning something here.
 *
 * cairo_select_font_face pulls in fontconfig, which builds a global pattern
 * cache and never frees it: 4738 bytes in 68 allocations, every one of them
 * inside libfontconfig, and none of them reachable from anything in this tree.
 * Suppressing by module rather than turning leak detection off means a real
 * leak in thVisual or in a visual plugin still fails the run -- which is the
 * whole reason this harness exists in a sanitizer build at all.
 */
extern "C" const char *__lsan_default_suppressions (void)
{
    return "leak:libfontconfig\n"
           "leak:FcInit\n"
           "leak:FcConfig\n"
           "leak:FcPattern\n"
           "leak:FcValueSave\n";
}

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

/* Not M_PI. It is not in C++'s <cmath> at all -- POSIX puts it in <math.h> and
   glibc obliges, MinGW's UCRT runtime does not unless _USE_MATH_DEFINES was
   defined first, and the Windows CI job duly failed here. think.h does that
   dance for the 27 places in the engine that need it, but this harness is
   deliberately free of think.h, so it carries its own. */
const double PI = 3.14159265358979323846;

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
        f.samples[i] = (float)sin(2.0 * PI * (double)i / 64.0);
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
        f.samples[i] = (float)sin(2.0 * PI * (double)i / 64.0);
    if (n > 3)
        f.samples[3] = (float)NAN;
    out.push_back(f);

    f.what = "denormals";
    f.samples.assign(n, 1e-40f);
    out.push_back(f);

    f.what = "a single sample";
    f.samples.assign(1, 0.25f);
    out.push_back(f);

    /* Deliberately much longer than the rest, and the only feed here whose
       content changes over time.
     *
     * Everything above is stationary, so a module that ignored the time axis
     * entirely would draw all of them correctly -- and the spectrogram is the
     * one module whose whole purpose is that axis. A sweep is also what anyone
     * would actually point one at: a filter opening is this shape.
     *
     * 16384 samples is 128 hops at the spectrogram's hop size, which is a
     * picture rather than a handful of columns. The extra length makes the
     * split-feed comparison meaningfully harder too. */
    f.what = "a rising sweep";
    f.samples.resize(16384);
    {
        double phase = 0.0;

        for (size_t i = 0; i < f.samples.size(); i++)
        {
            const double u = (double)i / (double)f.samples.size();

            /* 200Hz to 8kHz, exponentially -- a sweep that is linear in
               frequency spends nearly all of itself in the top octave and
               looks like a step. */
            const double hz = 200.0 * pow(40.0, u);

            phase += 2.0 * PI * hz / 44100.0;

            f.samples[i] = (float)(0.8 * sin(phase));
        }
    }
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
/* Set by render() and feedAll() when a module reports a failure through the
   ABI's return value. The host prints the first one; this is what turns it
   into a check rather than a line in the log. */
bool drawFailed = false;
bool feedFailed = false;

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

    /* Cleared to a known value first.
     *
     * cairo_image_surface_create does document that a new surface starts as
     * transparent black, so today this changes nothing -- but every property
     * below is a comparison of raw pixel bytes, and "a module that painted
     * nothing leaves no ink" is only true while that guarantee holds and is
     * remembered. One fill is cheaper than the assumption. */
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_restore(cr);

    if (visual.draw(inst, cr, w, h) != 0)
        drawFailed = true;

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

/* std::filesystem and config.h's PLUGIN_SUFFIX, which is what NodeCatalog::scan
   already does -- and for the reason that harness found the hard way. A
   hand-rolled dirent walk matching ".so" builds fine on macOS and then reports
   "no visual modules", because CMakeLists.txt sets the suffix to .dylib there
   and .dll on Windows. There is one place that knows the extension; ask it. */
void listVisuals (const string &dir, vector<string> &out)
{
    std::error_code ec;

    if (!fs::is_directory(dir, ec))
        return;

    for (const auto &f : fs::directory_iterator(dir, ec))
    {
        if (ec)
            break;

        if (f.path().extension() != PLUGIN_SUFFIX)
            continue;

        out.push_back(f.path().string());
    }

    /* Directory order is whatever the filesystem feels like, and a check whose
       output reorders itself between runs is a nuisance to diff. */
    std::sort(out.begin(), out.end());
}

/* ---- one module ---- */

/* Where to write pictures, or empty. See the -o comment in main. */
string pngDir;

void writePng (thVisual &visual, void *inst, const string &what)
{
    if (pngDir.empty())
        return;

    /* Big, because the point is to look at it. A panel is 128 wide in the
       editor and a module's bugs are not visible at that size -- this is the
       same module drawn large, not a different one. */
    const int W = 480, H = 200;

    /* The C API, like the rest of this file: visualcheck deliberately does not
       link cairomm, because the ABI it is testing takes a cairo_t. */
    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                       W, H);
    cairo_t *cr = cairo_create(surf);

    visual.draw(inst, cr, W, H);

    cairo_destroy(cr);

    string name = pngDir + "/" + visual.name() + "-" + what + ".png";

    for (size_t i = pngDir.size() + 1; i < name.size(); i++)
        if (name[i] == ' ' || name[i] == '+' || name[i] == '(' ||
            name[i] == ')')
            name[i] = '_';

    cairo_surface_write_to_png(surf, name.c_str());
    cairo_surface_destroy(surf);
}

void checkOne (const string &path)
{
    feedFailed = drawFailed = false;

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
        if (visual.feed(a, s, n) != 0)
            feedFailed = true;

        for (unsigned int at = 0; at < n; at += 37)
        {
            const unsigned int take = (n - at < 37) ? n - at : 37;

            if (visual.feed(b, s + at, take) != 0)
                feedFailed = true;
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

        writePng(visual, a, feeds[f].what);

        visual.close(a);
        visual.close(b);
    }

    /* The ABI's return values, which the host only reports once each -- so
       this is the thing that makes them a gate. A module that refuses every
       frame would otherwise be one line in a log nobody reads. */
    ok(!feedFailed, "%s: no feed reported a failure", visual.name().c_str());
    ok(!drawFailed, "%s: no draw reported a failure", visual.name().c_str());

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

/* ---- the shared FFT ----
 *
 * The one place in this work where "are the numbers right" is a question with
 * an answer, so it gets asked. Everything else here is about survival and
 * determinism, and the header says plainly that whether a *drawing* is correct
 * is a matter of looking at it -- but a transform either puts the energy in
 * the right bin at the right level or it does not.
 *
 * Worth having because both spectrum and spectrogram read off it, and because
 * a wrong scale factor or an off-by-one in the bit reversal produces a picture
 * that looks entirely plausible. A spectrum with every peak one bin low, or
 * 6dB down, is not something anyone would catch by eye.
 */
void checkFFT (void)
{
    const unsigned int ORDER = 10;

    thv::FFTR fft(ORDER);

    ok(fft.ok(), "the FFT allocates");

    if (!fft.ok())
        return;

    ok(fft.size() == 1024 && fft.bins() == 512,
       "order 10 is 1024 points and 512 bins (%u, %u)", fft.size(),
       fft.bins());

    vector<float> in(fft.size());
    vector<float> mag(fft.bins());

    struct Straight {
        const float *p;
        float operator() (unsigned int i) const { return p[i]; }
    };

    /* A full-scale sine exactly on bin k. On the bin, so there is no leakage
       to argue about and the level is unambiguous. */
    for (unsigned int k = 4; k <= 256; k *= 4)
    {
        for (unsigned int i = 0; i < fft.size(); i++)
            in[i] = (float)sin(2.0 * PI * (double)k * (double)i /
                               (double)fft.size());

        Straight at = { &in[0] };

        fft.magnitude(at, &mag[0]);

        unsigned int peak = 0;

        for (unsigned int b = 1; b < fft.bins(); b++)
            if (mag[b] > mag[peak])
                peak = b;

        ok(peak == k, "a sine on bin %u peaks at bin %u", k, peak);

        /* Within a fiftieth of a dB. Hann's coherent gain is exactly 0.5 and
           the scale factor undoes it, so this is not an approximation -- it is
           the identity the scale factor exists to produce. */
        ok(mag[peak] > 0.99f && mag[peak] < 1.01f,
           "and reads %.4f, not 1.0", (double)mag[peak]);

        /* And nothing else does. Two bins either side is the width of a Hann
           main lobe; beyond that a correct transform is down in the noise. */
        double worst = 0.0;

        for (unsigned int b = 0; b < fft.bins(); b++)
            if (b + 2 < peak || b > peak + 2)
                if (mag[b] > worst)
                    worst = mag[b];

        ok(worst < 0.01,
           "with everything outside the main lobe below -40dB (worst %.5f)",
           worst);
    }

    /* DC belongs in bin 0 and nowhere else -- the case an off-by-one in the
       bit reversal gets wrong in a way a sine does not. */
    {
        for (unsigned int i = 0; i < fft.size(); i++)
            in[i] = 0.5f;

        Straight at = { &in[0] };

        fft.magnitude(at, &mag[0]);

        unsigned int peak = 0;

        for (unsigned int b = 1; b < fft.bins(); b++)
            if (mag[b] > mag[peak])
                peak = b;

        ok(peak == 0, "DC peaks at bin 0, not %u", peak);
    }

    /* Silence in, silence out: no window leakage, no uninitialised bin. */
    {
        for (unsigned int i = 0; i < fft.size(); i++)
            in[i] = 0.0f;

        Straight at = { &in[0] };

        fft.magnitude(at, &mag[0]);

        double worst = 0.0;

        for (unsigned int b = 0; b < fft.bins(); b++)
            if (mag[b] > worst)
                worst = mag[b];

        ok(worst == 0.0, "silence transforms to silence (worst %.g)", worst);
    }
}

} /* namespace */

int main (int argc, char **argv)
{
    string pluginPath = PLUGIN_PATH;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-p")) { if (++i >= argc) return 2; pluginPath = argv[i]; }
        /* Writes each module's drawing of each feed, at a size worth looking
           at. Not a check and it asserts nothing -- the header above is clear
           that whether a visualizer is *correct* is a matter of looking at it,
           and this is what makes that possible without running the editor. */
        else if (!strcmp(argv[i], "-o")) { if (++i >= argc) return 2; pngDir = argv[i]; }
        else
        {
            printf("usage: %s [-p PLUGIN_PATH] [-o PNG_DIR]\n", argv[0]);
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

    checkFFT();

    printf("%d visual module(s) in %s\n", (int)modules.size(), dir.c_str());

    for (size_t i = 0; i < modules.size(); i++)
        checkOne(modules[i]);

    printf("\n%d checks, %d failed\n", checks, failures);

    return failures;
}
