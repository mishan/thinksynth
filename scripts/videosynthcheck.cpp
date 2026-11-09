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
 * videosynthcheck -- thVideoBridge hearing audio in windows of any length,
 * and thVideoSynth skipping ahead.
 *
 * The bridge: a 100 Hz sine on one channel and a 5 kHz one on another,
 * rendered in windows of 1024 samples and of 64, 128, 256 and 2048. Struck
 * late enough that the last 1024 samples are the same sound, and so that
 * the shorter windows' ring has wrapped part way, each says the same: the
 * bass and the high band lit, the mid dark, both channels' levels, all as
 * in 1024-sample windows.
 *
 * The skip: a video synth drawing misc::scan's x, y and frame, a frame and
 * a half in, skipped to frame 5. The next frame it hands over is frame 5,
 * whole, from its first pixel.
 */

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "think.h"
#include "thSynth.h"
#include "thUtil.h"
#include "thVideoBridge.h"
#include "thVideoSynth.h"

using std::string;

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

static string
sine (int hz)
{
    return "node ionode { channels = 1; play = 1; out0 = o->out; };\n"
           "node o osc::simple { freq = " + std::to_string(hz) +
           "; waveform = 0; amp = 1; };\n"
           "io ionode;\n";
}

/* After `samples' in windows of `window', the notes struck `late' in. */
static bool
hear (const string &plugins, int window, int samples, int late, thVideoInputs &in)
{
    thSynth synth(plugins, window, TH_DEFAULT_SAMPLES);
    thVideoBridge bridge;

    synth.setLimiter(false);
    synth.setMasterGain(1);
    if (synth.loadTreeText("low", sine(100), 0, MIDIVALMAX) == NULL ||
        synth.loadTreeText("high", sine(5000), 1, MIDIVALMAX) == NULL)
        return false;
    for (int n = 0; n < samples + late; n += window)
    {
        if (n == late)
        {
            synth.addNote(0, 60, MIDIVALMAX);
            synth.addNote(1, 60, MIDIVALMAX);
        }
        synth.process();
        bridge.hear(synth);
    }
    bridge.inputs(in);
    return true;
}

static void
checkBridge (const string &plugins)
{
    const int SAMPLES = 4096;
    thVideoInputs want;

    if (!hear(plugins, TH_DEFAULT_WINDOW_LENGTH, SAMPLES, 0, want))
    {
        report("the bridge's audio loads", "a sine did not load");
        return;
    }

    char why[200] = "";

    if (!(want.bass > 0.2f && want.high > 0.2f && want.mid < want.bass / 20 &&
          want.levels[0] > 0.1f && want.levels[1] > 0.1f && want.levels[2] == 0))
        snprintf(why, sizeof(why), "bass %g mid %g high %g, levels %g %g %g", want.bass,
                 want.mid, want.high, want.levels[0], want.levels[1], want.levels[2]);
    report("a low and a high sine, in windows of 1024", why);

    static const struct { int window, late; } rows[] = {
        { 64, 320 }, { 128, 384 }, { 256, 256 }, { 2048, 0 },
    };

    for (const auto &r : rows)
    {
        const int window = r.window;
        thVideoInputs got;

        why[0] = 0;
        hear(plugins, window, SAMPLES, r.late, got);

        const float gotv[] = { got.level, got.bass, got.mid, got.high, got.levels[0],
                               got.levels[1] };
        const float wantv[] = { want.level, want.bass, want.mid, want.high,
                                want.levels[0], want.levels[1] };
        static const char *const names[] = { "level", "bass", "mid", "high", "level0",
                                             "level1" };

        for (int k = 0; k < 6 && !why[0]; k++)
            if (fabsf(gotv[k] - wantv[k]) > 1e-5f * (1 + fabsf(wantv[k])))
                snprintf(why, sizeof(why), "%s is %g, not %g", names[k], gotv[k], wantv[k]);
        report("the same in windows of " + std::to_string(window), why);
    }
}

static void
checkSkip (const string &plugins)
{
    const int W = 8, H = 4, FPS = 10;
    const string path = thUtil::tempFile("videosynthcheck-") + ".dsp";

    {
        std::ofstream out(path);

        out << "node ionode { channels = 3; play = 1;"
               " out0 = s->x; out1 = s->y; out2 = s->frame; };\n"
               "node s misc::scan { };\n"
               "io ionode;\n";
    }

    thVideoSynth video;
    string why;

    if (!video.open(plugins, W, H, FPS, 1, why) || !video.follow(0, path, "", why))
    {
        std::remove(path.c_str());
        report("the scan loads", why);
        return;
    }
    std::remove(path.c_str());
    video.noteOn(0, 60, MIDIVALMAX, 1, NULL);

    /* A frame and a half: frame 0 handed over, frame 1 half drawn. */
    int64_t seen = -1;
    const auto none = [&](int64_t frame, const float *, const float *, const float *)
    {
        seen = frame;
        return true;
    };

    for (int k = 0; k < H * 3 / 2; k++)
        video.render(none);

    video.skipTo(5);

    char said[200] = "";
    bool whole = false;
    const auto check = [&](int64_t frame, const float *r, const float *g, const float *b)
    {
        whole = true;
        if (frame != 5)
            snprintf(said, sizeof(said), "the next frame is %lld, not 5", (long long)frame);
        for (int i = 0; i < W * H && !said[0]; i++)
            if (r[i] != (float)(i % W) / W || g[i] != (float)(i / W) / H || b[i] != 5)
                snprintf(said, sizeof(said), "pixel %d is x %g y %g frame %g, not %g %g 5",
                         i, r[i], g[i], b[i], (float)(i % W) / W, (float)(i / W) / H);
        return true;
    };

    for (int k = 0; k < H && !whole; k++)
        video.render(check);
    if (!whole)
        snprintf(said, sizeof(said), "no frame in %d lines", H);
    report("a skip to frame 5 hands over frame 5, whole", seen == 0 ? said
           : "before the skip, frame " + std::to_string(seen) + " was the last, not 0");
}

int
main (int argc, char *argv[])
{
    string plugins = PLUGIN_PATH;

    if (argc > 2 && !strcmp(argv[1], "-p"))
        plugins = argv[2];
    if (plugins.back() != '/')
        plugins += '/';

    checkBridge(plugins);
    checkSkip(plugins);

    return failed ? 1 : 0;
}
