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
 * dspbench -- how much of one core does a .dsp take?
 *
 *   build/scripts/dspbench -p build/plugins/ dsp/grand.dsp
 *
 * Two loads, each on a fresh synth:
 *
 * - chord: -n notes struck at once and held for -w windows. Prints
 *   addNote's time per note, the first window's time -- every voice's
 *   first window at once, which is where a note-on's cost lands on the
 *   audio thread -- and the mean, p99 and worst over the rest.
 *
 * - play: a note every -s ms up a scale across five octaves, each
 *   released -h ms later, so voices pile up in release the way a player's
 *   do. In milliseconds, rounded to whole windows, so that the notes are the
 *   same at any -l; -w is in windows, so scale it with -l for the same
 *   length of play. Prints the mean and worst window. -o writes its output,
 *   raw interleaved floats, for comparing two plugin sets sample by sample.
 *
 * -l and -r set the window length and the sample rate, the default
 * engine's otherwise; a browser runs 128 or 256 frames at 44.1 or 48 kHz.
 * Times are wall clock on the calling thread, against the window's own
 * length at that rate; on a loaded machine take the best of a few
 * runs. -v prints the chord's mean every two seconds, for a graph whose
 * cost changes as its voices decay. -j writes the inputs and both loads'
 * numbers to a file as one JSON object, for a script to compare runs.
 */

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <vector>

#include "think.h"

using namespace std;

static double now (void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* The play load's k-th note: a major scale up from A1, five octaves, then
   round again. */
static int playNote (int k)
{
    static const int scale[] = {0, 2, 4, 5, 7, 9, 11};

    return 33 + 12 * ((k / 7) % 5) + scale[k % 7];
}

static void jsonString (FILE *f, const char *s)
{
    fputc('"', f);

    for (; *s; s++)
    {
        if (*s == '"' || *s == '\\')
            fprintf(f, "\\%c", *s);
        else if ((unsigned char)*s < 0x20)
            fprintf(f, "\\u%04x", *s);
        else
            fputc(*s, f);
    }

    fputc('"', f);
}

int main (int argc, char **argv)
{
    string plugins = "plugins/";
    const char *file = NULL, *dump = NULL, *json = NULL;
    int voices = 16, windows = 400;
    double stepMs = 100, holdMs = 500;
    int windowlen = TH_DEFAULT_WINDOW_LENGTH, rate = TH_DEFAULT_SAMPLES;
    bool verbose = false;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-p") && i + 1 < argc)
            plugins = argv[++i];
        else if (!strcmp(argv[i], "-n") && i + 1 < argc)
            voices = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-w") && i + 1 < argc)
            windows = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-s") && i + 1 < argc)
            stepMs = atof(argv[++i]);
        else if (!strcmp(argv[i], "-h") && i + 1 < argc)
            holdMs = atof(argv[++i]);
        else if (!strcmp(argv[i], "-l") && i + 1 < argc)
            windowlen = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-r") && i + 1 < argc)
            rate = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-o") && i + 1 < argc)
            dump = argv[++i];
        else if (!strcmp(argv[i], "-j") && i + 1 < argc)
            json = argv[++i];
        else if (!strcmp(argv[i], "-v"))
            verbose = true;
        else if (argv[i][0] != '-' && file == NULL)
            file = argv[i];
        else
            file = NULL, i = argc;
    }

    if (file == NULL || voices < 1 || windows < 1 || !(stepMs > 0) ||
        !(holdMs >= 0) ||
        windowlen < 1 || rate < 1)
    {
        fprintf(stderr,
                "usage: dspbench [-p plugins/] [-n voices] [-w windows] "
                "[-s step_ms] [-h hold_ms] [-l windowlen] [-r rate] [-o play.f32] "
                "[-j out.json] [-v] file.dsp\n");
        return 2;
    }

    /* Opened before the timing, so a path that cannot be written is
       known before a run's worth of waiting rather than after. */
    FILE *jsonOut = NULL;

    if (json != NULL && (jsonOut = fopen(json, "w")) == NULL)
    {
        perror(json);
        return 1;
    }

    const double windowSec = (double)windowlen / rate;
    const int block = max(1, (int)(2.0 / windowSec + 0.5));
    const int step = max(1, (int)(stepMs / 1e3 / windowSec + 0.5));
    const int hold = (int)(holdMs / 1e3 / windowSec + 0.5);

    /* Each load's numbers, in ms, for -j. */
    double chordAdd, chordFirst, chordMean, chordP99, chordWorst;
    double playAdd, playMean, playWorst;

    /* chord */
    {
        thSynth synth(plugins, windowlen, rate);

        if (synth.loadTree(file, 0, 100) == NULL)
        {
            fprintf(stderr, "%s: did not load\n", file);
            return 1;
        }

        double add = 0, t, first, busy = 0, since = 0;
        vector<double> took(windows);

        /* Fifths up from C1, wrapping round 73 semitones, so no two of up
           to 73 voices share a pitch, at three velocities. */
        for (int i = 0; i < voices; i++)
        {
            t = now();
            synth.addNote(0, 24 + (i * 7) % 73, 40 + 40 * (i % 3));
            add += now() - t;
        }

        t = now();
        synth.process();
        first = now() - t;

        for (int w = 0; w < windows; w++)
        {
            t = now();
            synth.process();
            took[w] = now() - t;
            busy += took[w];
            since += took[w];

            if (verbose && (w + 1) % block == 0)
            {
                printf("  %4.0f s: %.3f ms/window\n", (w + 1) * windowSec,
                       1e3 * since / block);
                since = 0;
            }
        }

        sort(took.begin(), took.end());
        chordAdd = 1e3 * add / voices;
        chordFirst = 1e3 * first;
        chordMean = 1e3 * busy / windows;
        /* Nearest rank: below a hundred windows this is the worst
           one, which is why the count is printed beside it. */
        chordP99 = 1e3 * took[(size_t)ceil(0.99 * windows) - 1];
        chordWorst = 1e3 * took.back();
        printf("chord %d: %.3f ms/window, %.1f%% of real time, p99 %.3f ms "
               "of %d windows, worst %.3f ms; addNote %.3f ms; "
               "first window %.3f ms\n",
               voices, chordMean, 100 * busy / windows / windowSec,
               chordP99, windows, chordWorst, chordAdd, chordFirst);
    }

    /* play */
    {
        thSynth synth(plugins, windowlen, rate);

        synth.loadTree(file, 0, 100);

        double add = 0, t, busy = 0, worst = 0;
        int notes = 0;
        FILE *out = NULL;

        if (dump != NULL && (out = fopen(dump, "wb")) == NULL)
        {
            perror(dump);
            return 1;
        }

        for (int w = 0; w < windows; w++)
        {
            if (w % step == 0)
            {
                const int k = w / step;

                t = now();
                synth.addNote(0, playNote(k), 30 + 15 * ((k * 5) % 7));
                add += now() - t;
                notes++;
            }

            if (w >= hold && (w - hold) % step == 0)
                synth.delNote(0, playNote((w - hold) / step));

            t = now();
            synth.process();
            t = now() - t;
            busy += t;

            if (t > worst)
                worst = t;

            if (out)
                fwrite(synth.getOutput(), sizeof(float),
                       (size_t)synth.audioChannelCount() *
                           synth.getWindowlen(), out);
        }

        if (out)
            fclose(out);

        playAdd = 1e3 * add / notes;
        playMean = 1e3 * busy / windows;
        playWorst = 1e3 * worst;
        printf("play: %.3f ms/window, %.1f%% of real time, worst %.3f ms; "
               "addNote %.3f ms\n",
               playMean, 100 * busy / windows / windowSec, playWorst,
               playAdd);
    }

    if (jsonOut != NULL)
    {
        FILE *f = jsonOut;
        const double windowMs = 1e3 * windowSec;

        fputs("{\"dsp\": ", f);
        jsonString(f, file);
        fprintf(f, ", \"voices\": %d, \"windows\": %d, \"stepMs\": %g, "
                "\"holdMs\": %g, \"windowlen\": %d, \"rate\": %d,\n"
                " \"chord\": {\"addNoteMs\": %.4f, \"firstWindowMs\": %.4f, "
                "\"meanMs\": %.4f, \"p99Ms\": %.4f, \"worstMs\": %.4f, "
                "\"realTimePct\": %.2f},\n"
                " \"play\": {\"addNoteMs\": %.4f, \"meanMs\": %.4f, "
                "\"worstMs\": %.4f, \"realTimePct\": %.2f}}\n",
                voices, windows, stepMs, holdMs, windowlen, rate, chordAdd,
                chordFirst, chordMean, chordP99, chordWorst,
                100 * chordMean / windowMs, playAdd, playMean, playWorst,
                100 * playMean / windowMs);
        fclose(f);
    }

    return 0;
}
