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
 * Each row renders misc::scan's six outputs onto a six-wide synth with a
 * raster of some size, in windows of some size, and checks every sample
 * against the sample's own index n counted from the synth's start: x is
 * (n mod w) / w, y is (n / w mod h) / h, hsync and vsync mark a line's and
 * a frame's first pixel, frame is n / (w h), and t is n / rate.
 */

#include "config.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "think.h"
#include "thSynth.h"

using std::string;

static const char GRAPH[] =
    "node ionode { channels = 6; play = 1; out0 = s->x; out1 = s->y;"
    " out2 = s->hsync; out3 = s->vsync; out4 = s->frame; out5 = s->t; };\n"
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
        int w, h, window, windows;
    } rows[] = {
        { "a window a line long", 8, 4, 8, 12 },
        { "a window of lines", 8, 4, 16, 6 },
        { "a window across lines and frames", 5, 3, 7, 13 },
        { "a window longer than a frame", 4, 2, 11, 5 },
        { "no raster", 0, 0, 16, 3 },
    };
    const int RATE = 1000;

    for (const auto &r : rows)
    {
        thSynth synth(plugins, r.window, RATE, 6);

        synth.setRaster(r.w, r.h);
        synth.setLimiter(false);
        synth.setMasterGain(1);
        if (synth.loadTreeText("scancheck", GRAPH, 0, MIDIVALMAX) == NULL)
        {
            report(r.name, "the graph does not load");
            continue;
        }
        synth.addNote(0, 60, MIDIVALMAX);

        static const char *const names[] = { "x", "y", "hsync", "vsync", "frame", "t" };
        char why[160] = "";

        for (int k = 0; k < r.windows && !why[0]; k++)
        {
            synth.process();
            if (synth.position() != (int64_t)k * r.window)
            {
                snprintf(why, sizeof(why), "window %d begins at %lld, not %d", k,
                         (long long)synth.position(), k * r.window);
                break;
            }
            for (int i = 0; i < r.window && !why[0]; i++)
            {
                const long n = (long)k * r.window + i;
                const bool raster = r.w > 0;
                const double want[6] = {
                    raster ? (double)(n % r.w) / r.w : 0,
                    raster ? (double)(n / r.w % r.h) / r.h : 0,
                    raster && n % r.w == 0 ? 1.0 : 0.0,
                    raster && n % (r.w * r.h) == 0 ? 1.0 : 0.0,
                    raster ? (double)(n / (r.w * r.h)) : 0,
                    raster ? (double)n / RATE : 0,
                };

                for (int o = 0; o < 6; o++)
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
