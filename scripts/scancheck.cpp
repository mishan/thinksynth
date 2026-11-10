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
 * scancheck -- misc::scan, where on a video synth's raster each sample
 * lands, and the synth's count of samples it reads.
 *
 * Each row renders misc::scan's eight outputs onto an eight-wide synth with a
 * raster of some size, in windows of some size, and checks every sample
 * against the sample's own index n counted from the synth's start: x is
 * (n mod w) / w, y is (n / w mod h) / h, hsync and vsync mark a line's and
 * a frame's first pixel, frame is n / (w h), t is the frame's first
 * pixel's n / rate, so a frame is one moment, and width and size are w and
 * w h, what a frame-long delay is set by. A note
 * struck late reads where the synth is, not where the note began, and a
 * synth skipped ahead carries on from where the skip put it.
 */

#include "config.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "think.h"
#include "thSynth.h"

using std::string;

static const char GRAPH[] =
    "node ionode { channels = 8; play = 1; out0 = s->x; out1 = s->y;"
    " out2 = s->hsync; out3 = s->vsync; out4 = s->frame; out5 = s->t;"
    " out6 = s->width; out7 = s->size; };\n"
    "node s misc::scan { };\n"
    "io ionode;\n";

static int failed = 0;

static void
report (const string &name, const string &why)
{
    if (why.empty())
        printf("ok    %s\n", name.c_str());
    else
    {
        printf("FAIL  %s\n  %s\n", name.c_str(), why.c_str());
        failed++;
    }
}

int
main (int argc, char *argv[])
{
    string plugins = PLUGIN_PATH;

    if (argc > 2 && !strcmp(argv[1], "-p"))
        plugins = argv[2];
    if (plugins.back() != '/')
        plugins += '/';

    static const struct {
        const char *name;
        int w, h, window, windows, late, skipAt, skip;
    } rows[] = {
        { "a window a line long", 8, 4, 8, 12, 0, -1, 0 },
        { "a window of lines", 8, 4, 16, 6, 0, -1, 0 },
        { "a window across lines and frames", 5, 3, 7, 13, 0, -1, 0 },
        { "a window longer than a frame", 4, 2, 11, 5, 0, -1, 0 },
        { "a note struck three windows in", 5, 3, 7, 9, 3, -1, 0 },
        { "a skip of 23 samples four windows in", 5, 3, 7, 9, 0, 4, 23 },
        { "no raster", 0, 0, 16, 3, 0, -1, 0 },
    };
    const int RATE = 1000;

    for (const auto &r : rows)
    {
        thSynth synth(plugins, r.window, RATE, 8);

        synth.setRaster(r.w, r.h);
        synth.setLimiter(false);
        synth.setMasterGain(1);
        if (synth.loadTreeText("scancheck", GRAPH, 0, MIDIVALMAX) == NULL)
        {
            report(r.name, "the graph does not load");
            continue;
        }
        static const char *const names[] = { "x", "y", "hsync", "vsync", "frame", "t",
                                             "width", "size" };
        char why[160] = "";

        for (int k = 0; k < r.windows && !why[0]; k++)
        {
            const int skipped = r.skipAt >= 0 && k >= r.skipAt ? r.skip : 0;

            if (k == r.late)
                synth.addNote(0, 60, MIDIVALMAX);
            if (k == r.skipAt)
                synth.skip(r.skip);
            synth.process();
            if (synth.position() != (int64_t)k * r.window + skipped)
            {
                snprintf(why, sizeof(why), "window %d begins at %lld, not %d", k,
                         (long long)synth.position(), k * r.window + skipped);
                break;
            }
            for (int i = 0; i < r.window && !why[0] && k >= r.late; i++)
            {
                const long n = (long)k * r.window + i + skipped;
                const bool raster = r.w > 0;
                const double want[8] = {
                    raster ? (double)(n % r.w) / r.w : 0,
                    raster ? (double)(n / r.w % r.h) / r.h : 0,
                    raster && n % r.w == 0 ? 1.0 : 0.0,
                    raster && n % (r.w * r.h) == 0 ? 1.0 : 0.0,
                    raster ? (double)(n / (r.w * r.h)) : 0,
                    raster ? (double)(n - n % (r.w * r.h)) / RATE : 0,
                    (double)r.w,
                    (double)r.w * r.h,
                };

                for (int o = 0; o < 8; o++)
                {
                    const float got = synth.getOutput()[o * r.window + i];

                    if (got != (float)want[o])
                    {
                        snprintf(why, sizeof(why), "sample %ld: %s is %g, not %g",
                                 n, names[o], got, (float)want[o]);
                        break;
                    }
                }
            }
        }
        report(string(r.name) + (r.w ? ", " + std::to_string(r.w) + "x" +
                                 std::to_string(r.h) : string()) +
               ", in windows of " + std::to_string(r.window), why);
    }

    /* A silent synth renders nothing, and still counts. */
    {
        thSynth synth(plugins, 32, RATE);

        synth.setSilent(true);
        for (int k = 0; k < 3; k++)
            synth.process();
        report("a silent synth counts its windows too",
               synth.position() == 64 ? ""
               : "the third window begins at " + std::to_string(synth.position()));
    }

    return failed ? 1 : 0;
}
