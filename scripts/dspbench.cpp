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
 * dspbench -- how much of one core does a .dsp take?
 *
 *   build/scripts/dspbench -p build/plugins/ dsp/grand.dsp
 *
 * Two loads, each on a fresh synth:
 *
 * - chord: -n notes struck at once and held for -w windows. Prints
 *   addNote's time per note, the first window's time -- every voice's
 *   first window at once, which is where a note-on's cost lands on the
 *   audio thread -- and the mean over the rest.
 *
 * - play: a note every -s windows up a scale across five octaves, each
 *   released -h windows later, so voices pile up in release the way a
 *   player's do. Prints the mean and worst window. -o writes its output,
 *   raw interleaved floats, for comparing two plugin sets sample by sample.
 *
 * Times are wall clock on the calling thread, against the window's own
 * length at the default rate; on a loaded machine take the best of a few
 * runs. -v prints the chord's mean every two seconds, for a graph whose
 * cost changes as its voices decay.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

int main (int argc, char **argv)
{
    string plugins = "plugins/";
    const char *file = NULL, *dump = NULL;
    int voices = 16, windows = 400, step = 4, hold = 20;
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
            step = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-h") && i + 1 < argc)
            hold = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-o") && i + 1 < argc)
            dump = argv[++i];
        else if (!strcmp(argv[i], "-v"))
            verbose = true;
        else if (argv[i][0] != '-' && file == NULL)
            file = argv[i];
        else
            file = NULL, i = argc;
    }

    if (file == NULL || voices < 1 || windows < 1 || step < 1 || hold < 0)
    {
        fprintf(stderr,
                "usage: dspbench [-p plugins/] [-n voices] [-w windows] "
                "[-s step] [-h hold] [-o play.f32] [-v] file.dsp\n");
        return 2;
    }

    const double windowSec =
        (double)TH_DEFAULT_WINDOW_LENGTH / TH_DEFAULT_SAMPLES;
    const int block = (int)(2.0 / windowSec + 0.5);

    /* chord */
    {
        thSynth synth(plugins, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

        if (synth.loadTree(file, 0, 100) == NULL)
        {
            fprintf(stderr, "%s: did not load\n", file);
            return 1;
        }

        double add = 0, t, first, since;

        /* Fifths up from C2, so no two voices share a pitch, at three
           velocities. */
        for (int i = 0; i < voices; i++)
        {
            t = now();
            synth.addNote(0, 36 + (i * 7) % 60, 40 + 40 * (i % 3));
            add += now() - t;
        }

        t = now();
        synth.process();
        first = now() - t;

        t = since = now();

        for (int w = 0; w < windows; w++)
        {
            synth.process();

            if (verbose && (w + 1) % block == 0)
            {
                printf("  %4.0f s: %.3f ms/window\n", (w + 1) * windowSec,
                       1e3 * (now() - since) / block);
                since = now();
            }
        }

        t = now() - t;
        printf("chord %d: %.3f ms/window, %.1f%% of real time; "
               "addNote %.3f ms; first window %.3f ms\n",
               voices, 1e3 * t / windows, 100 * t / windows / windowSec,
               1e3 * add / voices, 1e3 * first);
    }

    /* play */
    {
        thSynth synth(plugins, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

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
                       synth.audioChannelCount() * synth.getWindowlen(), out);
        }

        if (out)
            fclose(out);

        printf("play: %.3f ms/window, %.1f%% of real time, worst %.3f ms; "
               "addNote %.3f ms\n",
               1e3 * busy / windows, 100 * busy / windows / windowSec,
               1e3 * worst, 1e3 * add / notes);
    }

    return 0;
}
