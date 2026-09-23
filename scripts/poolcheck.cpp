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
 * poolcheck -- a restarted voice sounds like a new one.
 *
 *   scripts/poolcheck -p plugins/ $(find dsp -name '*.dsp')
 *
 * A channel keeps the voices the audio thread has finished with and starts
 * them over for later notes (thMidiChan::recycle), resetting each arg in the
 * buffer it already has rather than copying the prototype again. The claim
 * is that nothing can tell: every plugin keeps its state in args, and a
 * restored arg is what the copy constructor would have made.
 *
 * So each file plays the same notes twice, on fresh synths -- a note every
 * three windows up and down the keyboard, each held for six, long enough
 * for voices to finish and be started over -- once with the pool and once
 * with every note a fresh copy (thSynth::setVoicePool), and the two renders
 * must be bitwise equal.
 *
 * A file whose voices outlast the run is never restarted, and says so; it
 * passes, having shown nothing. The run fails if no file restarted a voice.
 *
 * Exit status is the number of files that differ, or 1 if nothing was
 * restarted at all.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "think.h"

#define WINDOWS 300
#define STEP 3
#define HOLD 6

/* The k-th note: up and down three octaves in fifths. */
static int noteAt (int k)
{
    const int span = 36 / 7 + 1;
    const int up = k % (2 * span);

    return 36 + 7 * (up < span ? up : 2 * span - 1 - up);
}

static bool render (const string &plugins, const char *file, bool pool,
                    vector<float> &out, unsigned long *restarts)
{
    thSynth synth(plugins, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
    thSynthTree *tree = synth.loadTree(file, 0, 100);

    out.clear();
    *restarts = 0;

    if (tree == NULL || tree->takesInput())
        return false;

    synth.setVoicePool(pool);

    const int frame = synth.audioChannelCount() * synth.getWindowlen();

    for (int w = 0; w < WINDOWS; w++)
    {
        if (w % STEP == 0)
        {
            const int k = w / STEP;

            synth.addNote(0, noteAt(k), 40 + 20 * (k % 4));
        }

        if (w >= HOLD && (w - HOLD) % STEP == 0)
            synth.delNote(0, noteAt((w - HOLD) / STEP));

        synth.process();

        const float *buf = synth.getOutput();

        out.insert(out.end(), buf, buf + frame);
    }

    if (synth.getChannel(0) != NULL)
        *restarts = synth.getChannel(0)->restarts();

    return true;
}

int main (int argc, char **argv)
{
    string plugins = "plugins/";
    int differ = 0, same = 0, skipped = 0;
    unsigned long total = 0;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-p") && i + 1 < argc)
        {
            plugins = argv[++i];
            continue;
        }

        vector<float> pooled, fresh;
        unsigned long restarts, none;

        if (!render(plugins, argv[i], true, pooled, &restarts) ||
            !render(plugins, argv[i], false, fresh, &none))
        {
            printf("skip  %s\n", argv[i]);
            skipped++;
            continue;
        }

        total += restarts;

        if (pooled.size() == fresh.size() &&
            !memcmp(pooled.data(), fresh.data(),
                    pooled.size() * sizeof(float)))
        {
            printf("same  %-34s %lu restarted\n", argv[i], restarts);
            same++;
            continue;
        }

        size_t first = 0;

        while (first < pooled.size() && first < fresh.size() &&
               !memcmp(&pooled[first], &fresh[first], sizeof(float)))
            first++;

        printf("DIFF  %-34s %lu restarted, first differing sample %zu\n",
               argv[i], restarts, first);
        differ++;
    }

    printf("\n%d identical, %d differ, %d skipped; %lu voices restarted\n",
           same, differ, skipped, total);

    if (differ == 0 && same > 0 && total == 0)
    {
        printf("no voice was restarted, so nothing was shown\n");
        return 1;
    }

    return differ;
}
