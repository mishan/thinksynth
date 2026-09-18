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
 * dspsweep -- every shipped graph, at the corners of every control it
 * declares, looking for one thing: a sample that is not a number.
 *
 *     scripts/dspsweep -p plugins/ $(find dsp -name "*.dsp")
 *     scripts/dspsweep -p plugins/ -g scripts/guard dsp/ts1.dsp
 *
 * A filter whose coefficients leave the stable region, or an envelope
 * dividing by a zero-length segment, produces an infinity and then a NaN.
 * That is not one bad voice but a bad mix: NaN plus anything is NaN, so every
 * other voice on the channel goes with it and the master limiter turns the
 * result into silence. The symptom is a render that is quiet for no stated
 * reason. `filt::res2pole2' under a res of about 0.5 and `brass.dsp' with a
 * zero-length bend were both found that way, the slow way.
 *
 * Every control a .dsp declares is driven to its `.min', its `.max' and three
 * points between, with one note at the bottom of the keyboard and one at the
 * top; the per-voice guard (thMidiChan::mixNote) is then asked whether it
 * caught anything. One control moves at a time -- what goes non-finite is a
 * coefficient leaving its own range, not two knobs conspiring.
 *
 * Controls, not chanargs: `name', `author' and `description' are chanargs too
 * and are strings. `.widget' is the line the format already draws between the
 * two (DSP_FORMAT.md).
 *
 * `-g DIR' first runs the guard's own gate over the two graphs in
 * scripts/guard: one that cannot help going non-finite and one that behaves,
 * on two channels at once. The claim is that the bad voice costs its own note
 * and nothing else -- the good channel keeps its peak. genwav's exit status
 * on the same piece is a test of its own; see scripts/CMakeLists.txt.
 *
 * Exit status is the number of failures: a file that did not load, a case
 * that went non-finite, or a broken claim in the guard's gate.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <filesystem>
#include <string>
#include <vector>

#include "think.h"

/* The two ends of the keyboard. A filter's stable region depends on its
   cutoff and a .dsp routinely derives that from the note, so these are two
   cases rather than two samples of one. */
#define NOTE_LOW   21
#define NOTE_HIGH  108

/* Long enough for an attack to finish and a filter to settle, short enough
   that a corpus-wide sweep stays a gate. A run-away reaches infinity in tens
   of samples, not thousands. */
#define WINDOWS    8

/* min, max, and three points between. */
#define POINTS     5

/* The note scripts/guard/nonfinite.dsp poisons, and one it does not. */
#define NOTE_BAD   60
#define NOTE_GOOD  72

struct Control {
    std::string name;
    float min, max;
};

/* The channel's controls, in the order the tree declares them. */
static void
controlsOf (thSynth &synth, int chan, std::vector<Control> &out)
{
    const thArgMap args = synth.getChanArgs(chan);

    for (thArgMap::const_iterator i = args.begin(); i != args.end(); i++)
    {
        const thArg *arg = i->second;

        if (arg == NULL || arg->widgetType() == thArg::HIDE)
            continue;

        /* A range that is a point is not a sweep, and a range the wrong way
           round is a declaration nobody meant. Either way there is nothing
           here to walk. */
        if (!(arg->max() > arg->min()))
            continue;

        Control c;

        c.name = i->first;
        c.min = arg->min();
        c.max = arg->max();

        out.push_back(c);
    }
}

/* One note, held for WINDOWS windows and then taken away. Returns the number
   of voices the guard dropped while it sounded. */
static unsigned long
playOne (thSynth &synth, int note, int windows)
{
    const unsigned long before = synth.nonFiniteVoices();

    synth.addNote(0, (float)note, 100);

    for (int w = 0; w < windows; w++)
        synth.process();

    /* Not delNote: a released note goes on sounding through its release, and
       the next case would be measuring this one's tail as well as its own. */
    synth.clearAll();
    synth.process();

    return synth.nonFiniteVoices() - before;
}

/* Every corner of every control of one file. Returns the number of failures,
   and adds the number of cases run to `cases'. */
static int
sweepFile (const std::string &pluginPath, const char *file, int windows,
           int points, bool quiet, long &cases)
{
    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

    if (synth.loadTree(file, 0, 100) == NULL)
    {
        printf("FAIL  %s: did not load\n", file);
        return 1;
    }

    std::vector<Control> controls;

    controlsOf(synth, 0, controls);

    static const int notes[2] = { NOTE_LOW, NOTE_HIGH };

    int failures = 0;

    /* The settings the file ships with, before any control is moved.
     *
     * Not a formality: the sweep below moves one control at a time and leaves
     * every other at its declared value, so the combination a graph is
     * actually played at is the one case none of those cover. A regression
     * that only shows at the defaults would otherwise pass here and retire a
     * voice on the first note anybody played. */
    for (int n = 0; n < 2; n++)
    {
        cases++;

        if (playOne(synth, notes[n], windows) > 0)
        {
            printf("FAIL  %s: note %d at the file's own settings went "
                   "non-finite\n", file, notes[n]);
            failures++;
        }
    }

    for (size_t c = 0; c < controls.size(); c++)
    {
        thArg *arg = synth.getChanArg(0, controls[c].name);

        if (arg == NULL)
            continue;

        /* Put it back afterwards. One control moves at a time, so what every
           other control is set to has to be what the file said. */
        const float saved = (arg->len() > 0) ? (*arg)[0] : 0.0f;

        for (int p = 0; p < points; p++)
        {
            const float v = controls[c].min +
                (controls[c].max - controls[c].min) * p / (float)(points - 1);

            /* A single-float write does not reallocate, which is why a
               slider may do it. */
            arg->setValue(v);

            for (int n = 0; n < 2; n++)
            {
                cases++;

                if (playOne(synth, notes[n], windows) == 0)
                    continue;

                printf("FAIL  %s: @%s = %g, note %d went non-finite\n",
                       file, controls[c].name.c_str(), v, notes[n]);
                failures++;
            }
        }

        arg->setValue(saved);
    }

    if (!quiet && failures == 0)
        printf("ok    %s (%zu control%s)\n", file, controls.size(),
               controls.size() == 1 ? "" : "s");

    return failures;
}

/* The highest |sample| on one channel's own mix, before the master gain and
   limiter. -1 for a channel that went non-finite. */
static float
channelPeak (thSynth &synth, int chan)
{
    thMidiChan *c = synth.getChannel(chan);

    if (c == NULL || c->output() == NULL)
        return -1.0f;

    const size_t n = thOutputSamples(c->numChannels(), synth.getWindowlen());

    float peak = 0;

    for (size_t i = 0; i < n; i++)
    {
        const float a = fabsf(c->output()[i]);

        /* A NaN fails every comparison, so a plain peak loop reports it as
           zero. Say so instead. */
        if (!thIsFinite(a))
            return -1.0f;

        if (a > peak)
            peak = a;
    }

    return peak;
}

/* The guard's own gate. Two arrangements, because the guard makes two claims
 * and only one of them is about channels.
 *
 * Per voice: nonfinite.dsp poisons middle C and nothing else, so one channel
 * can hold a bad voice and a good one at the same time. The good one has to go
 * on sounding -- that is the claim, and a guard that merely isolated the whole
 * channel would fail it. A gate that put the two voices on separate channels
 * could not tell the two apart, which is what this one used to do.
 *
 * Per channel: a second channel playing an ordinary instrument keeps its peak
 * while the first is losing voices.
 *
 * Returns the number of failures.
 */
static int
checkGuard (const std::string &pluginPath, const std::string &dir,
            int windows, bool quiet)
{
    const std::string bad = (std::filesystem::path(dir) /
                             "nonfinite.dsp").string();
    const std::string good = (std::filesystem::path(dir) /
                              "sound.dsp").string();

    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

    if (synth.loadTree(bad, 0, 100) == NULL ||
        synth.loadTree(good, 1, 100) == NULL)
    {
        printf("FAIL  guard: %s and %s did not both load\n", bad.c_str(),
               good.c_str());
        return 1;
    }

    /* Channel 0 takes both: middle C, which nonfinite.dsp cannot render, and
       an octave above it, which it renders like any other instrument. */
    synth.addNote(0, NOTE_BAD, 100);
    synth.addNote(0, NOTE_GOOD, 100);
    synth.addNote(1, NOTE_BAD, 100);

    float mixedPeak = 0, otherPeak = 0;
    bool mixedWentNonFinite = false, otherWentNonFinite = false;

    for (int w = 0; w < windows; w++)
    {
        synth.process();

        const float m = channelPeak(synth, 0);
        const float o = channelPeak(synth, 1);

        if (m < 0)
            mixedWentNonFinite = true;
        else if (m > mixedPeak)
            mixedPeak = m;

        if (o < 0)
            otherWentNonFinite = true;
        else if (o > otherPeak)
            otherPeak = o;
    }

    int failures = 0;

    if (synth.nonFiniteVoices() == 0)
    {
        printf("FAIL  guard: the guard never fired on %s\n", bad.c_str());
        failures++;
    }

    if (mixedWentNonFinite)
    {
        printf("FAIL  guard: a non-finite sample reached the channel sum\n");
        failures++;
    }
    else if (mixedPeak <= 0)
    {
        printf("FAIL  guard: the good voice was lost with the bad one -- the "
               "channel peaked at %g\n", mixedPeak);
        failures++;
    }

    if (otherWentNonFinite || otherPeak <= 0)
    {
        printf("FAIL  guard: the second channel peaked at %g beside a channel "
               "losing voices\n",
               otherWentNonFinite ? -1.0f : otherPeak);
        failures++;
    }

    if (failures == 0 && !quiet)
        printf("ok    guard: %lu voice%s dropped; its neighbour on the same "
               "channel held %.3f, the other channel %.3f\n",
               synth.nonFiniteVoices(),
               synth.nonFiniteVoices() == 1 ? "" : "s", mixedPeak, otherPeak);

    return failures;
}

static void
usage (const char *argv0)
{
    printf("usage: %s [-p PATH] [-w WINDOWS] [-n POINTS] [-g DIR] [-q] "
           "file.dsp ...\n"
           "\n"
           "  -p, --plugin-path PATH  where to find plugin .so files\n"
           "  -w, --windows N         windows to hold each note (default %d)\n"
           "  -n, --points N          points across each control's range "
           "(default %d)\n"
           "  -g, --guard DIR         also run the guard's own gate over the "
           "graphs in DIR\n"
           "  -q, --quiet             only say what failed\n",
           argv0, WINDOWS, POINTS);
}

int
main (int argc, char **argv)
{
    std::string pluginPath = PLUGIN_PATH;
    std::string guardDir;
    int windows = WINDOWS;
    int points = POINTS;
    bool quiet = false;
    int firstFile = -1;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-p") || !strcmp(argv[i], "--plugin-path"))
        {
            if (++i >= argc) { usage(argv[0]); return 2; }
            pluginPath = argv[i];
        }
        else if (!strcmp(argv[i], "-w") || !strcmp(argv[i], "--windows"))
        {
            if (++i >= argc) { usage(argv[0]); return 2; }
            windows = atoi(argv[i]);
        }
        else if (!strcmp(argv[i], "-n") || !strcmp(argv[i], "--points"))
        {
            if (++i >= argc) { usage(argv[0]); return 2; }
            points = atoi(argv[i]);
        }
        else if (!strcmp(argv[i], "-g") || !strcmp(argv[i], "--guard"))
        {
            if (++i >= argc) { usage(argv[0]); return 2; }
            guardDir = argv[i];
        }
        else if (!strcmp(argv[i], "-q") || !strcmp(argv[i], "--quiet"))
            quiet = true;
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))
        {
            usage(argv[0]);
            return 0;
        }
        else
        {
            firstFile = i;
            break;
        }
    }

    if (windows < 1 || points < 2)
    {
        usage(argv[0]);
        return 2;
    }

    if (firstFile < 0 && guardDir.empty())
    {
        usage(argv[0]);
        return 2;
    }

    if (pluginPath.empty() || pluginPath[pluginPath.size() - 1] != '/')
        pluginPath += '/';

    int failures = 0;
    long cases = 0;

    if (!guardDir.empty())
        failures += checkGuard(pluginPath, guardDir, windows, quiet);

    for (int f = firstFile; f > 0 && f < argc; f++)
        failures += sweepFile(pluginPath, argv[f], windows, points, quiet,
                              cases);

    if (!quiet || failures > 0)
        printf("\n%ld case%s swept, %d failure%s\n", cases,
               cases == 1 ? "" : "s", failures, failures == 1 ? "" : "s");

    return failures;
}
