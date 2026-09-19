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
 * dspab -- renders the same note through two sets of plugins and compares the
 * audio bitwise.
 *
 * For answering "did that plugin change alter the sound?" with a measurement
 * instead of an argument. Build the .so files twice, keep a copy of the first
 * set, and point this at both:
 *
 *   scripts/dspab -a /tmp/plugins-base/ -b plugins/ $(find dsp -name '*.dsp')
 *
 * -A and -B set each side's window length, and the two sides may share a
 * plugin directory, which turns this into the other question a plugin can
 * be asked -- does it sound the same cut into windows of 256 as of 1024?
 *
 *   scripts/dspab -a plugins/ -b plugins/ -B 256 $(find dsp -name '*.dsp')
 *
 * The browser build runs at 256 (docs/JAM.md). -w counts windows of the
 * default length, so both sides render the same number of frames whatever
 * their windows are, and the renders are compared interleaved, since the
 * synth's own window is planar and two lengths of it only line up frame by
 * frame.
 *
 * Exit status is the number of files that differ. Note that a DSP using
 * osc::static is only deterministic because each render builds a fresh
 * synth, which restarts its noise, and anything calling rand() only because
 * both renders reseed; see dspcheck.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <vector>

#include "think.h"

/* One note, `frames' frames of it, interleaved. */
static bool renderNote (const string &pluginPath, const char *file,
                        int windowlen, int frames, vector<float> &out)
{
    srand(1);

    thSynth synth(pluginPath, windowlen, TH_DEFAULT_SAMPLES);

    if (synth.loadTree(file, 0, 100) == NULL)
        return false;

    synth.addNote(0, 60, 100);

    const int channels = synth.audioChannelCount();
    const int len = synth.getWindowlen();

    out.clear();
    out.reserve((size_t)frames * channels);

    for (int done = 0; done < frames; )
    {
        synth.process();

        const float *buf = synth.getOutput();

        for (int i = 0; i < len && done < frames; i++, done++)
            for (int c = 0; c < channels; c++)
                out.push_back(buf[(size_t)c * len + i]);
    }

    return true;
}

int main (int argc, char **argv)
{
    string pathA, pathB;
    int windows = 8;
    int lenA = TH_DEFAULT_WINDOW_LENGTH, lenB = TH_DEFAULT_WINDOW_LENGTH;
    bool quiet = false;
    int firstFile = -1;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-a")) { if (++i >= argc) return 2; pathA = argv[i]; }
        else if (!strcmp(argv[i], "-b")) { if (++i >= argc) return 2; pathB = argv[i]; }
        else if (!strcmp(argv[i], "-A")) { if (++i >= argc) return 2; lenA = atoi(argv[i]); }
        else if (!strcmp(argv[i], "-B")) { if (++i >= argc) return 2; lenB = atoi(argv[i]); }
        else if (!strcmp(argv[i], "-w")) { if (++i >= argc) return 2; windows = atoi(argv[i]); }
        else if (!strcmp(argv[i], "-q")) quiet = true;
        else { firstFile = i; break; }
    }

    if (pathA.empty() || pathB.empty() || firstFile < 0 ||
        lenA <= 0 || lenB <= 0 || windows <= 0)
    {
        printf("usage: %s -a PLUGINS_A -b PLUGINS_B [-A WINDOW] [-B WINDOW] "
               "[-w N] [-q] file.dsp ...\n", argv[0]);
        return 2;
    }

    const int frames = windows * TH_DEFAULT_WINDOW_LENGTH;

    if (pathA[pathA.size() - 1] != '/') pathA += '/';
    if (pathB[pathB.size() - 1] != '/') pathB += '/';

    int differ = 0, same = 0, skipped = 0;

    for (int f = firstFile; f < argc; f++)
    {
        vector<float> a, b;

        if (!renderNote(pathA, argv[f], lenA, frames, a) ||
            !renderNote(pathB, argv[f], lenB, frames, b))
        { skipped++; continue; }

        if (a.size() != b.size())
        {
            printf("DIFF  %s: %d samples vs %d\n", argv[f],
                   (int)a.size(), (int)b.size());
            differ++;
            continue;
        }

        size_t firstBad = a.size();
        double worst = 0;

        for (size_t i = 0; i < a.size(); i++)
            if (memcmp(&a[i], &b[i], sizeof(float)) != 0)
            {
                if (firstBad == a.size())
                    firstBad = i;

                const double d = fabs((double)a[i] - (double)b[i]);

                if (d > worst)
                    worst = d;
            }

        if (firstBad != a.size())
        {
            printf("DIFF  %-34s first differing frame %d of %d, worst %.6g\n",
                   argv[f], (int)(firstBad / 2), frames, worst);
            differ++;
        }
        else
        {
            same++;
            if (!quiet)
                printf("same  %-34s %d samples\n", argv[f], (int)a.size());
        }
    }

    printf("\n%d identical, %d differ, %d skipped\n", same, differ, skipped);

    return differ;
}
