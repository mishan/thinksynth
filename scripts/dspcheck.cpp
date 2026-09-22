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
 * dspcheck -- headless harness for the parser and the synth graph.
 *
 * Loads each .dsp named on the command line onto a MIDI channel, plays a few
 * notes through it, and tears everything down. Files ending in .patch are run
 * through gthPatchManager instead, which also exercises the DSP they name. No
 * audio backend and no GUI, so it runs clean under ASan/UBSan/valgrind in CI
 * or over a whole directory:
 *
 *     make -C scripts dspcheck-asan
 *     scripts/dspcheck --plugin-path plugins/ $(find dsp -name '*.dsp')
 *     scripts/dspcheck --plugin-path plugins/ $(find patches -name '*.patch')
 *
 * --shipped adds the one rule that is about this tree rather than about the
 * format: every graph shipped here declares a `category', and it is one of
 * the documented list. The format takes free text -- a .dsp of somebody's own
 * says whatever it likes -- so the flag is what separates "the corpus" from
 * "a .dsp", and the fixtures in scripts/guard are swept without it.
 *
 * Exit status is the number of files that failed to load.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"
#include "gthPatchfile.h"

/* Renders one note into `out' (windows * channels * windowlen floats). */
static bool renderNote (const string &pluginPath, const char *file,
                        int windows, vector<float> &out)
{
    /* Both renders happen in this one process. The noise plugins' generators
       -- osc::static's and osc::noise's -- restart whenever a synth loads
       one, which the fresh synth below does; the
       reseed is for anything else that calls rand(), whose second run would
       otherwise continue the sequence and look non-deterministic. */
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

/* Renders the same note twice, from a fresh synth each time, and compares.
 *
 * Any difference means the graph read memory nobody had written -- a plugin
 * picking up whatever was in a freshly allocated state buffer. That is not
 * something ASan catches (it tracks addresses, not initialisation), and it is
 * audible as a burst of noise on a note's attack that clears once the note
 * sustains and the buffers hold real audio.
 */
static bool checkDeterminism (const string &pluginPath, const char *file,
                              int windows, int &firstBadWindow)
{
    vector<float> a, b;

    if (!renderNote(pluginPath, file, windows, a))
        return true;   /* load failures are reported elsewhere */

    if (!renderNote(pluginPath, file, windows, b))
        return true;

    if (a.size() != b.size())
    {
        firstBadWindow = 0;
        return false;
    }

    const int frame = (int)(a.size() / (windows ? windows : 1));

    for (size_t i = 0; i < a.size(); i++)
    {
        /* Bitwise: two runs of the same deterministic graph should agree
           exactly, and NaN != NaN would otherwise slip through. */
        if (memcmp(&a[i], &b[i], sizeof(float)) != 0)
        {
            firstBadWindow = frame ? (int)(i / frame) : 0;
            return false;
        }
    }

    return true;
}

/* Where a shipped graph may be filed.
 *
 * The groups mostly fall out of the description lines, and the arguable ones
 * are arguable in one direction each: a plucked string is its own thing
 * rather than a lead, and a graph promoted out of the old drawers because it
 * was interesting rather than because it was useful is an experiment and
 * says so. Effects is dsp/fx/, which is the one place a directory already
 * carries meaning the format relies on.
 *
 * A list and not an enum. `category' is free text in the grammar; this is the
 * discipline the shipped corpus is held to and nothing else, which is the
 * difference between a category and a schema. See docs/DSP_FORMAT.md
 * "Choosing a file". */
static const char *const kCategories[] = {
    "Bass", "Drums", "Effects", "Experiments", "Keys", "Leads and stabs",
    "Plucked", "Strings and pads", "Synths", NULL
};

static bool knownCategory (const string &category)
{
    for (int i = 0; kCategories[i] != NULL; i++)
        if (category == kCategories[i])
            return true;

    return false;
}

static string categoryList (void)
{
    string out;

    for (int i = 0; kCategories[i] != NULL; i++)
        out += string(i ? ", " : "") + "'" + kCategories[i] + "'";

    return out;
}

static void usage (const char *argv0)
{
    /* PLUGIN_PATH is an argument rather than part of the format. It is a
       compile-time install path, so it is data: a prefix with a `%' in it
       would otherwise be read as a conversion and eat an argument that was
       never passed. */
    printf("usage: %s [-p|--plugin-path PATH] [-w WINDOWS] file.dsp ...\n"
           "\n"
           "  -p, --plugin-path PATH  where to find plugin .so files\n"
           "                          (default: %s)\n"
           "  -w, --windows N         process N windows per file (default 8)\n"
           "  -q, --quiet             only report failures\n"
           "      --shipped           these are this tree's own graphs: each\n"
           "                          must declare a documented category\n",
           argv0, PLUGIN_PATH);
}

int main (int argc, char **argv)
{
    string pluginPath = PLUGIN_PATH;
    int windows = 8;
    bool quiet = false;
    bool shipped = false;
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
        else if (!strcmp(argv[i], "-q") || !strcmp(argv[i], "--quiet"))
        {
            quiet = true;
        }
        else if (!strcmp(argv[i], "--shipped"))
        {
            shipped = true;
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

    if (firstFile < 0)
    {
        usage(argv[0]);
        return 2;
    }

    /* A trailing slash is not optional -- thPluginManager concatenates. */
    if (pluginPath.empty() || pluginPath[pluginPath.size() - 1] != '/')
        pluginPath += '/';

    int failed = 0;
    int total = 0;

    for (int i = firstFile; i < argc; i++)
    {
        const char *file = argv[i];

        total++;

        /* A fresh synth per file: loading N DSPs into one synth would share
           thSynthTree instances between channels, which is its own bug. */
        thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH,
                      TH_DEFAULT_SAMPLES);

        size_t len = strlen(file);
        bool isPatch = (len > 6 && !strcmp(file + len - 6, ".patch"));

        thSynthTree *tree = NULL;

        if (isPatch)
        {
            /* gthPatchManager reaches the synth through thSynth::instance(),
               which the constructor above has already set. */
            gthPatchManager patchMgr;

            if (!patchMgr.loadPatch(file, 0))
            {
                printf("FAIL  %s (patch did not load)\n", file);
                failed++;
                continue;
            }

            tree = synth.getChannel(0) ? synth.getChannel(0)->modnode() : NULL;
        }
        else
        {
            tree = synth.loadTree(file, 0, 100);
        }

        /* Copy anything we want to report now. The channel owns its tree, so
           reloading the channel below frees this pointer. */
        string treeName = (tree != NULL) ? tree->name() : string();

        /* Where it is filed. Only for this tree's own graphs, and only for a
           .dsp: a .patch inherits its graph's category and has none of its
           own to check here. */
        if (shipped && !isPatch && tree != NULL)
        {
            if (tree->category().empty())
            {
                printf("FAIL  %s (declares no category; one of %s)\n",
                       file, categoryList().c_str());
                failed++;
            }
            else if (!knownCategory(tree->category()))
            {
                printf("FAIL  %s (filed under '%s', which is not one of "
                       "%s)\n", file, tree->category().c_str(),
                       categoryList().c_str());
                failed++;
            }
        }

        if (tree == NULL)
        {
            printf("FAIL  %s (did not load)\n", file);
            failed++;
            continue;
        }

        /* Exercise the graph the way the MIDI path would: a chord on, a few
           windows of processing, note off, a few more windows so the release
           stage and the decaying-note list both get walked. */
        synth.addNote(0, 60, 100);
        synth.addNote(0, 64, 90);
        synth.addNote(0, 67, 80);

        for (int w = 0; w < windows; w++)
            synth.process();

        synth.delNote(0, 64);

        for (int w = 0; w < windows; w++)
            synth.process();

        /* Overrun the polyphony limit so the note-stealing paths run. */
        for (int n = 40; n < 60; n++)
            synth.addNote(0, n, 64);

        for (int w = 0; w < windows; w++)
            synth.process();

        synth.clearAll();
        synth.process();

        /* Reload onto the same channel: this is the patch-switch path, where
           the old thMidiChan is destroyed under live arg pointers. */
        if (!isPatch)
        {
            synth.loadTree(file, 0, 100);
            synth.addNote(0, 72, 100);
            synth.process();

            /* The same .dsp on a second channel. These used to share one
               thSynthTree, so channel 1's construction overwrote channel 0's
               cached chanarg pointers, and tearing down either one left the
               other dereferencing freed thArgs. Play both, then drop one and
               keep processing the survivor -- that is where it showed up. */
            if (synth.loadTree(file, 1, 100) != NULL)
            {
                synth.addNote(1, 55, 110);
                synth.addNote(0, 67, 90);

                for (int w = 0; w < windows; w++)
                    synth.process();

                synth.removeChan(1);

                for (int w = 0; w < windows; w++)
                    synth.process();
            }
        }

        synth.removeChan(0);

        /* Same note, twice, from scratch -- must come out identical. */
        if (!isPatch)
        {
            int badWindow = -1;

            if (!checkDeterminism(pluginPath, file, 4, badWindow))
            {
                printf("FAIL  %s (non-deterministic output, first differing "
                       "window %d -- a plugin is reading uninitialised state)\n",
                       file, badWindow);
                failed++;
                continue;
            }
        }

        if (!quiet)
            printf("ok    %s (%s)\n", file, treeName.c_str());
    }

    printf("\n%d/%d loaded, %d failed\n", total - failed, total, failed);

    return failed;
}
