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
 * dsplevel -- how far past full scale does a DSP run?
 *
 * thSynth::process sums every sounding note into one buffer with no headroom
 * management: each voice contributes up to TH_MAX scaled only by the channel
 * amplitude. Two voices therefore reach roughly twice full scale, three about
 * three times, and so on.
 *
 * That used to reach the output stage unclamped. The ALSA path casts float to
 * signed short, and converting an out-of-range float to an integer type is
 * undefined -- in practice it wraps, so a sample at +1.234 came out near
 * -25102. Every overshoot became a full-scale sign flip. That was the static
 * on a note's attack: loudest at the envelope peak, gone once the note decayed
 * back inside the rails, and worse with every extra voice held down.
 *
 * thClampSample() now guards both output paths, so the wraparound is gone.
 * This tool measures what is still being clipped -- the gain staging that
 * clamping papers over.
 *
 *   make -C scripts
 *   LD_LIBRARY_PATH=libthink scripts/dsplevel -p plugins/ dsp/ts1.dsp
 *
 * Exit status is the number of measurements that still exceed TH_MAX after the
 * master limiter, which should be none.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

struct LevelResult {
    float peak;      /* highest |sample| after the master limiter          */
    int peakWindow;
    long over;       /* samples past TH_MAX -- should always be zero now   */
    long shaped;     /* samples the limiter actually bent (past the knee)  */
    long total;
    float peakCutDb; /* how hard the limiter worked at the peak, in dB      */
};

/* Invert thSoftLimit to recover the pre-limiter value, so the report can say
   how much gain reduction actually happened rather than just how many samples
   crossed the knee -- a sample at 0.78 only moves to 0.778, which is nothing,
   and counting it alongside one pulled down from 3.1 is misleading. */
static float unLimit (float y)
{
    const float knee = TH_LIMIT_KNEE;
    const float range = (float)TH_MAX - knee;

    if (y <= knee)
        return y;

    const float t = (y - knee) / range;

    /* atanh diverges as the peak approaches TH_MAX, and a float peak lands
       exactly on 1.0 easily, so cap rather than report a nonsense figure. */
    if (t >= 0.99995f)
        return -1.0f;

    return knee + range * atanhf(t);
}

static bool measure (const string &pluginPath, const char *file, int voices,
                     int windows, LevelResult &result)
{
    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

    if (synth.loadTree(file, 0, 100) == NULL)
        return false;

    /* A chord rather than repeats of one note: the same note twice would just
       steal itself under the polyphony rules. */
    static const int notes[] = { 60, 64, 67, 72, 55, 48, 79, 43 };
    const int maxVoices = (int)(sizeof(notes) / sizeof(notes[0]));

    for (int i = 0; i < voices && i < maxVoices; i++)
        synth.addNote(0, (float)notes[i], 100);

    const int frame = synth.audioChannelCount() * synth.getWindowlen();

    result.peak = 0;
    result.peakWindow = -1;
    result.over = 0;
    result.shaped = 0;
    result.total = 0;
    result.peakCutDb = 0;

    for (int w = 0; w < windows; w++)
    {
        synth.process();

        const float *buf = synth.getOutput();

        for (int i = 0; i < frame; i++)
        {
            const float a = fabsf(buf[i]);

            if (a > result.peak)
            {
                result.peak = a;
                result.peakWindow = w;
            }

            if (a > (float)TH_MAX)
                result.over++;

            if (a > TH_LIMIT_KNEE)
                result.shaped++;

            result.total++;
        }
    }

    if (result.peak > TH_LIMIT_KNEE)
    {
        const float before = unLimit(result.peak);

        if (before < 0.0f)
            result.peakCutDb = -1.0f;      /* saturated; report as a floor */
        else if (before > result.peak && result.peak > 0)
            result.peakCutDb = 20.0f * log10f(before / result.peak);
    }

    return true;
}

static void usage (const char *argv0)
{
    printf("usage: %s [-p PATH] [-v VOICES] [-w WINDOWS] file.dsp ...\n"
           "\n"
           "  -p, --plugin-path PATH  where to find plugin .so files\n"
           "  -v, --voices N          highest voice count to try (default 4)\n"
           "  -w, --windows N         windows to render (default 12)\n",
           argv0);
}

int main (int argc, char **argv)
{
    string pluginPath = PLUGIN_PATH;
    int maxVoices = 4;
    int windows = 12;
    int firstFile = -1;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-p") || !strcmp(argv[i], "--plugin-path"))
        {
            if (++i >= argc) { usage(argv[0]); return 2; }
            pluginPath = argv[i];
        }
        else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--voices"))
        {
            if (++i >= argc) { usage(argv[0]); return 2; }
            maxVoices = atoi(argv[i]);
        }
        else if (!strcmp(argv[i], "-w") || !strcmp(argv[i], "--windows"))
        {
            if (++i >= argc) { usage(argv[0]); return 2; }
            windows = atoi(argv[i]);
        }
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

    if (firstFile < 0) { usage(argv[0]); return 2; }

    if (pluginPath.empty() || pluginPath[pluginPath.size() - 1] != '/')
        pluginPath += '/';

    int hot = 0;

    for (int f = firstFile; f < argc; f++)
    {
        printf("%s\n", argv[f]);

        for (int v = 1; v <= maxVoices; v++)
        {
            LevelResult r;

            if (!measure(pluginPath, argv[f], v, windows, r))
            {
                printf("  (did not load)\n");
                break;
            }

            char cut[32];

            if (r.peakCutDb < 0.0f)
                snprintf(cut, sizeof(cut), "  >20 dB");
            else
                snprintf(cut, sizeof(cut), "%5.1f dB", r.peakCutDb);

            printf("  %d voice%s peak %6.3f  shaped %5.1f%% of samples  "
                   "peak cut %s%s\n",
                   v, (v == 1) ? ": " : "s:", r.peak,
                   r.total ? (100.0 * r.shaped / r.total) : 0.0, cut,
                   (r.over > 0) ? "   <- STILL CLIPPING" : "");

            /* Nothing should get past the limiter any more. */
            if (r.over > 0)
                hot++;
        }
    }

    return hot;
}
