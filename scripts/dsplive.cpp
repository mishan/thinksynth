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
 * dsplive -- does moving a control actually change the sound?
 *
 * The node editor pushes slider moves straight into the running synth, and
 * "you can hear it" is not something the writer tests or the graph tests can
 * say anything about. This renders the same note twice:
 *
 *   A: note on, render N windows, touch nothing
 *   B: note on, render N/2 windows, move a control, render N/2 more
 *
 * and asserts that the first halves are bitwise identical -- nothing drifted
 * on its own -- while the second halves differ. That is exactly the claim:
 * the change reaches a note that is already sounding, and nothing else moved.
 *
 * A control that no node reads, or one a node reads but that does not affect
 * the output for this note, will correctly show no difference; those are
 * reported separately rather than as failures.
 *
 *   make -C scripts
 *   LD_LIBRARY_PATH=libthink scripts/dsplive -p plugins/ dsp/ts1.dsp
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <vector>

#include "think.h"
#include "NodeGraph.h"

/* Renders one note. If `arg' is given, its value is set to `value' halfway
   through, while the note is sounding. */
static bool render (const string &pluginPath, const char *file, int windows,
                    const string &arg, float value, vector<float> &out)
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
        if (!arg.empty() && w == windows / 2)
        {
            thArg *a = synth.getChanArg(0, arg);

            if (a == NULL)
                return false;

            /* The same call the node editor's slider makes, and the same one
               the keyboard's own sliders have always made. */
            a->setValue(value);
        }

        synth.process();

        const float *buf = synth.getOutput();

        out.insert(out.end(), buf, buf + frame);
    }

    return true;
}

int main (int argc, char **argv)
{
    string pluginPath = PLUGIN_PATH;
    int windows = 16;
    bool quiet = false;
    int firstFile = -1;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-p")) { if (++i >= argc) return 2; pluginPath = argv[i]; }
        else if (!strcmp(argv[i], "-w")) { if (++i >= argc) return 2; windows = atoi(argv[i]); }
        else if (!strcmp(argv[i], "-q")) quiet = true;
        else { firstFile = i; break; }
    }

    if (firstFile < 0)
    {
        printf("usage: %s [-p PATH] [-w N] [-q] file.dsp ...\n", argv[0]);
        return 2;
    }

    if (pluginPath.empty() || pluginPath[pluginPath.size() - 1] != '/')
        pluginPath += '/';

    int failed = 0, heard = 0, silent = 0, files = 0, skipped = 0;

    for (int f = firstFile; f < argc; f++)
    {
        thSynth probe(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

        thSynthTree *tree = probe.parseTree(argv[f]);

        if (tree == NULL) { skipped++; continue; }

        NodeGraph g;

        g.build(tree);
        delete tree;

        vector<float> base;

        if (!render(pluginPath, argv[f], windows, "", 0, base))
        { skipped++; continue; }

        files++;

        int problems = 0;

        for (size_t b = 0; b < g.boxes().size() && problems < 3; b++)
        {
            const NodeGraph::Box &bx = g.boxes()[b];

            if (!bx.isControl)
                continue;

            /* The far end of the declared range, so the change is as audible
               as the control allows. */
            float target = (fabs((double)(bx.ctlMax - bx.ctlValue)) >
                            fabs((double)(bx.ctlValue - bx.ctlMin)))
                           ? bx.ctlMax : bx.ctlMin;

            if (target == bx.ctlValue)
                continue;

            vector<float> moved;

            if (!render(pluginPath, argv[f], windows, bx.ctlArg, target, moved))
            { printf("FAIL  %s: @%s is not reachable on the live channel\n",
                     argv[f], bx.ctlArg.c_str());
              problems++; continue; }

            if (moved.size() != base.size())
            { printf("FAIL  %s: @%s changed the output length\n", argv[f],
                     bx.ctlArg.c_str());
              problems++; continue; }

            const size_t half = (base.size() / windows) * (windows / 2);

            /* Before the move, the two renders must be the same sample for
               sample. If they are not, something other than the control is
               drifting and the rest of this proves nothing. */
            if (memcmp(&base[0], &moved[0], half * sizeof(float)) != 0)
            { printf("FAIL  %s: @%s -- the renders differ before the move\n",
                     argv[f], bx.ctlArg.c_str());
              problems++; continue; }

            bool differs = false;

            for (size_t i = half; i < base.size() && !differs; i++)
                if (memcmp(&base[i], &moved[i], sizeof(float)) != 0)
                    differs = true;

            if (differs)
            {
                heard++;

                if (!quiet)
                    printf("heard %-28s @%-12s %g -> %g\n", argv[f],
                           bx.ctlArg.c_str(), (double)bx.ctlValue,
                           (double)target);
            }
            else
            {
                /* Not a failure: plenty of controls only matter to a part of
                   the patch this note does not exercise. */
                silent++;

                if (!quiet)
                    printf("      %-28s @%-12s no audible change\n", argv[f],
                           bx.ctlArg.c_str());
            }
        }

        if (problems)
            failed++;
    }

    printf("\n%d files, %d failed, %d skipped\n", files, failed, skipped);
    printf("  %d controls changed the sound of a ringing note, "
           "%d had no effect on it\n", heard, silent);

    return failed;
}
