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
 * rendered in windows of 1024 samples and of 64, 100, 128, 256 and 2048. Struck
 * late enough that the last 1024 samples are the same sound, and so that
 * the shorter windows' ring has wrapped part way, each says the same: the
 * bass and the high band lit, the mid dark, both channels' levels, all as
 * in 1024-sample windows.
 *
 * The skip: a video synth drawing misc::scan's x, y and frame, a frame and
 * a half in, skipped to frame 5. The next frame it hands over is frame 5,
 * whole, from its first pixel.
 *
 * The voices: five notes held on a patch that draws 1 a voice. It draws
 * two, a video channel's limit, and five where its io says poly = 5.
 *
 * The slow inputs: a synth's first level, 0.5, is @in_level_slow at
 * once. Struck to 1, it climbs a window at a time, in a frame neither
 * still at 0.5 nor there yet, never falling, and there within a second;
 * after a skip, it is where the sound is.
 */

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

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
    /* Whole windows of every length below. */
    const int SAMPLES = 51200;
    thVideoInputs want;

    if (!hear(plugins, TH_DEFAULT_WINDOW_LENGTH, SAMPLES, 0, want))
    {
        report("the bridge's audio loads", "a sine did not load");
        return;
    }

    char why[200] = "";

    if (!(want.bass > 0.2f && want.high > 0.2f && want.mid < want.bass / 20 &&
          want.levels[0] > 0.1f && want.levels[1] > 0.1f))
        snprintf(why, sizeof(why), "bass %g mid %g high %g, levels %g %g", want.bass,
                 want.mid, want.high, want.levels[0], want.levels[1]);
    report("a low and a high sine, in windows of 1024", why);

    static const struct { int window, late; } rows[] = {
        { 64, 320 }, { 100, 300 }, { 128, 384 }, { 256, 256 }, { 2048, 0 },
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

    if (!video.open(plugins, W, H, FPS, 1, why) || !video.follow(0, path, why))
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

static void
checkVoices (const string &plugins)
{
    const struct
    {
        const char *poly;
        float voices;
    } CASES[] = { { "", 2 }, { " poly = 5;", 5 } };

    for (const auto &c : CASES)
    {
        const string name = string("five notes held") + (c.poly[0] ? " under poly = 5" : "") +
                            " draw " + std::to_string((int)c.voices) + " voices";
        thVideoSynth video;
        string why;

        if (!video.open(plugins, 8, 4, 10, 1, why) ||
            !video.followText(0, "voices", string("node ionode { channels = 3;") + c.poly +
                              " play = 1; out0 = 1; out1 = 1; out2 = 1; }; io ionode;",
                              why))
        {
            report(name, why);
            continue;
        }
        for (int note = 60; note < 65; note++)
            video.noteOn(0, note, MIDIVALMAX, 1, NULL);

        float drawn = -1;
        const auto keep = [&](int64_t frame, const float *r, const float *, const float *)
        {
            if (frame == 1)
                drawn = r[0];
            return true;
        };

        for (int k = 0; k < 4 * 2; k++)
            video.render(keep);
        report(name, drawn == c.voices ? "" : "it drew " + std::to_string(drawn));
    }
}

/* A one-channel patch draws in red alone, as the warning it gets says: a
   picture's planes are its colors, so its one plane is not spread. */
static void
checkMonoRed (const string &plugins)
{
    thVideoSynth video;
    string why;

    if (!video.open(plugins, 8, 4, 10, 1, why) ||
        !video.followText(0, "mono", "node ionode { channels = 1; play = 1; out0 = 0.5; };"
                          " io ionode;", why))
    {
        report("a one-channel patch draws in red alone", why);
        return;
    }
    video.noteOn(0, 60, MIDIVALMAX, 1, NULL);

    float rgb[3] = { -1, -1, -1 };
    const auto keep = [&](int64_t frame, const float *r, const float *g, const float *b)
    {
        if (frame == 1)
        {
            rgb[0] = r[0];
            rgb[1] = g[0];
            rgb[2] = b[0];
        }
        return true;
    };

    for (int k = 0; k < 4 * 2; k++)
        video.render(keep);
    report("a one-channel patch draws in red alone",
           rgb[0] == 0.5f && rgb[1] == 0 && rgb[2] == 0 ? "" :
           "it drew " + std::to_string(rgb[0]) + " " + std::to_string(rgb[1]) + " " +
           std::to_string(rgb[2]));
}

static void
checkSlow (const string &plugins)
{
    const int W = 8, H = 4, FPS = 10;
    const string name = "@in_level_slow climbs to a level struck at once, a window at a time";
    thVideoSynth video;
    string why;

    if (!video.open(plugins, W, H, FPS, 1, why) ||
        !video.followText(0, "slow", "@in_level_slow = 0;"
                          " node ionode { channels = 3; play = 1;"
                          " out0 = @in_level_slow; out1 = 0; out2 = 0; };"
                          " io ionode;", why))
    {
        report(name, why);
        return;
    }
    video.noteOn(0, 60, MIDIVALMAX, 1, NULL);

    thVideoInputs in;
    float took = -1;
    const auto one = [&](int64_t, const float *r, const float *, const float *)
    {
        took = r[0];
        return true;
    };

    in.level = 0.5f;
    video.setInputs(in);
    for (int k = 0; k < H; k++)
        video.render(one);
    report("a synth's first level is @in_level_slow at once",
           took == 0.5f ? "" : "it is " + std::to_string(took));
    in.level = 1;
    video.setInputs(in);

    std::vector<float> seen;
    const auto keep = [&](int64_t, const float *r, const float *, const float *)
    {
        for (int y = 0; y < H; y++)
            seen.push_back(r[y * W]);
        return true;
    };

    for (int k = 0; k < H * FPS; k++)
        video.render(keep);

    char said[200] = "";

    if (seen.size() != (size_t)H * FPS)
        snprintf(said, sizeof(said), "%zu windows drawn, not %d", seen.size(), H * FPS);
    else if (!(seen[0] > 0.5f && seen[H - 1] < 0.97f && seen[0] < seen[H - 1]))
        snprintf(said, sizeof(said), "the first frame runs %g to %g", seen[0], seen[H - 1]);
    else if (seen.back() < 0.99f)
        snprintf(said, sizeof(said), "a second on, it is %g", seen.back());
    for (size_t i = 1; i < seen.size() && !said[0]; i++)
        if (seen[i] < seen[i - 1])
            snprintf(said, sizeof(said), "window %zu falls to %g from %g", i, seen[i],
                     seen[i - 1]);
    report(name, said);

    in.level = 0.25f;
    video.setInputs(in);
    video.skipTo(video.frames() + 2);

    float after = -1;
    const auto first = [&](int64_t, const float *r, const float *, const float *)
    {
        after = r[0];
        return true;
    };

    for (int k = 0; k < H; k++)
        video.render(first);
    report("after a skip, @in_level_slow is where the sound is",
           after == 0.25f ? "" : "it is " + std::to_string(after));
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
    checkVoices(plugins);
    checkMonoRed(plugins);
    checkSlow(plugins);

    return failed ? 1 : 0;
}
