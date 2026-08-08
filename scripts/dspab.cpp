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
 * Exit status is the number of files that differ. Note that a DSP using
 * osc::static or anything else calling rand() is only deterministic because
 * both renders reseed; see dspcheck.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <vector>

#include "think.h"

static bool renderNote (const string &pluginPath, const char *file,
                        int windows, vector<float> &out)
{
    srand(1);

    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

    if (synth.loadTree(file, 0, 100) == NULL)
        return false;

    synth.addNote(0, 60, 100);

    const int frame = synth.audioChannelCount() * synth.getWindowlen();

    out.clear();

    for (int w = 0; w < windows; w++)
    {
        synth.process();

        const float *buf = synth.getOutput();

        out.insert(out.end(), buf, buf + frame);
    }

    return true;
}

int main (int argc, char **argv)
{
    string pathA, pathB;
    int windows = 8;
    bool quiet = false;
    int firstFile = -1;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-a")) { if (++i >= argc) return 2; pathA = argv[i]; }
        else if (!strcmp(argv[i], "-b")) { if (++i >= argc) return 2; pathB = argv[i]; }
        else if (!strcmp(argv[i], "-w")) { if (++i >= argc) return 2; windows = atoi(argv[i]); }
        else if (!strcmp(argv[i], "-q")) quiet = true;
        else { firstFile = i; break; }
    }

    if (pathA.empty() || pathB.empty() || firstFile < 0)
    {
        printf("usage: %s -a PLUGINS_A -b PLUGINS_B [-w N] [-q] file.dsp ...\n",
               argv[0]);
        return 2;
    }

    if (pathA[pathA.size() - 1] != '/') pathA += '/';
    if (pathB[pathB.size() - 1] != '/') pathB += '/';

    int differ = 0, same = 0, skipped = 0;

    for (int f = firstFile; f < argc; f++)
    {
        vector<float> a, b;

        if (!renderNote(pathA, argv[f], windows, a) ||
            !renderNote(pathB, argv[f], windows, b))
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
            printf("DIFF  %-34s first differing sample %d of %d, worst %.6g\n",
                   argv[f], (int)firstBad, (int)a.size(), worst);
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
