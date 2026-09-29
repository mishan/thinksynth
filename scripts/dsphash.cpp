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
 * dsphash -- renders a short phrase through each .dsp and prints a hash of
 * the audio, one line per file.
 *
 *   scripts/dsphash -p plugins/ -i dsp/supersaw.dsp $(find dsp -name '*.dsp')
 *   scripts/dsphash-embedded -i dsp/supersaw.dsp $(find dsp -name '*.dsp')
 *
 * Built twice from this one source: dsphash against libthink, which
 * dlopens the plugins under -p, and dsphash-embedded against
 * think_embedded, which has them compiled in and ignores -p. The two
 * cannot share a process -- which loader libthink has is a compile-time
 * choice -- so the comparison is of their output, and the `embedded' gate
 * (cmake/RunHarness.cmake, HARNESS_B) fails on any line that differs. A
 * plugin that behaves differently compiled into an archive than as a
 * module, or a row of the table pointing at the wrong plugin, changes a
 * hash.
 *
 * The phrase: a note, a second one over it two windows later, and the first
 * released two windows after that, so attack, overlap and release all
 * reach the hash. An effect graph (one that takes input) has nothing of its
 * own to play, so it goes on channel 1 over the -i instrument playing the
 * phrase, with the same instrument on channel 2 as its side channel -- a
 * vocoder's carrier, a compressor's key. Each file gets a fresh synth,
 * which restarts osc::static's noise, and srand is reseeded; see dspab.
 *
 * Exit status is the number of files that did not load.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdint.h>

#include "think.h"

#define DH_WINDOWS 8

/* FNV-1a, 64-bit, over the bytes of every sample. */
static uint64_t hashBytes (uint64_t h, const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;

    for (size_t i = 0; i < len; i++)
    {
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }

    return h;
}

/* True if `file' parses as an effect graph. */
static bool isEffect (const string &pluginPath, const char *file)
{
    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
    thSynthTree *tree = synth.parseTree(file);
    const bool effect = tree != NULL && tree->takesInput();

    delete tree;

    return effect;
}

static bool renderPhrase (const string &pluginPath, const char *file,
                          const char *source, uint64_t &hash)
{
    srand(1);

    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

    if (source == NULL)
    {
        if (synth.loadTree(file, 0, 100) == NULL)
            return false;
    }
    else if (synth.loadTree(source, 0, 100) == NULL ||
             synth.loadTree(source, 1, 100) == NULL ||
             synth.loadEffect(file, 0, 1) == NULL)
        return false;

    const size_t samples = thOutputSamples(synth.audioChannelCount(),
                                           synth.getWindowlen());

    hash = 0xcbf29ce484222325ULL;

    for (int w = 0; w < DH_WINDOWS; w++)
    {
        if (w == 0)
        {
            synth.addNote(0, 60, 100);

            if (source != NULL)
                synth.addNote(1, 48, 100);
        }
        else if (w == 2)
            synth.addNote(0, 67, 90);
        else if (w == 4)
            synth.delNote(0, 60);

        synth.process();

        hash = hashBytes(hash, synth.getOutput(), samples * sizeof(float));
    }

    return true;
}

int main (int argc, char **argv)
{
    string pluginPath;
    const char *source = NULL;
    int firstFile = -1;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-p"))
        {
            if (++i >= argc)
                return 2;
            pluginPath = argv[i];
        }
        else if (!strcmp(argv[i], "-i"))
        {
            if (++i >= argc)
                return 2;
            source = argv[i];
        }
        else
        {
            firstFile = i;
            break;
        }
    }

    if (firstFile < 0)
    {
        printf("usage: %s [-p PLUGINS] [-i INSTRUMENT] file.dsp ...\n",
               argv[0]);
        return 2;
    }

    if (!pluginPath.empty() && pluginPath[pluginPath.size() - 1] != '/')
        pluginPath += '/';

    int failed = 0;

    for (int f = firstFile; f < argc; f++)
    {
        uint64_t hash;
        const bool effect = isEffect(pluginPath, argv[f]);

        /* An effect with no -i to put it over is a failure rather than a
           skip: it would otherwise hash as silence and match anything. */
        if (effect && source == NULL)
        {
            printf("FAIL (no -i)      %s\n", argv[f]);
            failed++;
            continue;
        }

        if (!renderPhrase(pluginPath, argv[f], effect ? source : NULL, hash))
        {
            printf("FAIL              %s\n", argv[f]);
            failed++;
            continue;
        }

        printf("%016llx  %s\n", (unsigned long long)hash, argv[f]);
    }

    return failed;
}
