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
 * dspgraph -- builds and lays out the node graph for each .dsp, headlessly.
 *
 * Layout quality is subjective and cannot be asserted. These properties can:
 *
 *   - every node in the tree gets a box
 *   - every wire is attached to a real port at both ends
 *   - an input port never has two wires into it (the .dsp grammar cannot
 *     express that, so if it happens the builder is wrong)
 *   - no two boxes overlap after layout
 *   - no plugin-internal state (ARG_STATE) is exposed as a port
 *
 * Exit status is the number of files with a failure.
 *
 *   make -C scripts
 *   LD_LIBRARY_PATH=libthink scripts/dspgraph -p plugins/ $(find dsp -name '*.dsp')
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"
#include "NodeGraph.h"

static bool overlaps (const NodeGraph::Box &a, const NodeGraph::Box &b)
{
    return !(a.x + a.w <= b.x || b.x + b.w <= a.x ||
             a.y + a.h <= b.y || b.y + b.h <= a.y);
}

int main (int argc, char **argv)
{
    string pluginPath = PLUGIN_PATH;
    bool quiet = false;
    int firstFile = -1;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-p") || !strcmp(argv[i], "--plugin-path"))
        {
            if (++i >= argc) return 2;
            pluginPath = argv[i];
        }
        else if (!strcmp(argv[i], "-q") || !strcmp(argv[i], "--quiet"))
            quiet = true;
        else { firstFile = i; break; }
    }

    if (firstFile < 0)
    {
        printf("usage: %s [-p PATH] [-q] file.dsp ...\n", argv[0]);
        return 2;
    }

    if (pluginPath.empty() || pluginPath[pluginPath.size() - 1] != '/')
        pluginPath += '/';

    int failed = 0, total = 0, skipped = 0;
    long boxTotal = 0, edgeTotal = 0, fbTotal = 0, fbFiles = 0;
    int maxLayers = 0, maxBoxes = 0;

    for (int f = firstFile; f < argc; f++)
    {
        thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

        thSynthTree *tree = synth.loadTree(argv[f], 0, 100);

        if (tree == NULL) { skipped++; continue; }

        total++;

        NodeGraph g;

        if (!g.build(tree))
        {
            printf("FAIL  %s (build failed)\n", argv[f]);
            failed++;
            continue;
        }

        g.layout();

        const vector<NodeGraph::Box> &boxes = g.boxes();
        const vector<NodeGraph::Edge> &edges = g.edges();

        int problems = 0;

        /* every wire attached at both ends, to ports facing the right way */
        for (size_t e = 0; e < edges.size(); e++)
        {
            const NodeGraph::Edge &ed = edges[e];

            if (ed.fromBox < 0 || ed.fromBox >= (int)boxes.size() ||
                ed.toBox < 0 || ed.toBox >= (int)boxes.size())
            { printf("FAIL  %s: edge %d has a bad box\n", argv[f], (int)e);
              problems++; continue; }

            const NodeGraph::Box &fb = boxes[ed.fromBox];
            const NodeGraph::Box &tb = boxes[ed.toBox];

            if (ed.fromPort < 0 || ed.fromPort >= (int)fb.ports.size() ||
                ed.toPort < 0 || ed.toPort >= (int)tb.ports.size())
            { printf("FAIL  %s: edge %d has a bad port\n", argv[f], (int)e);
              problems++; continue; }

            if (fb.ports[ed.fromPort].isInput)
            { printf("FAIL  %s: edge %d leaves an input port (%s.%s)\n", argv[f],
                     (int)e, fb.name.c_str(), fb.ports[ed.fromPort].name.c_str());
              problems++; }

            if (!tb.ports[ed.toPort].isInput)
            { printf("FAIL  %s: edge %d enters an output port (%s.%s)\n", argv[f],
                     (int)e, tb.name.c_str(), tb.ports[ed.toPort].name.c_str());
              problems++; }
        }

        /* one wire per input: the grammar cannot express more */
        {
            map<pair<int,int>, int> fanIn;

            for (size_t e = 0; e < edges.size(); e++)
                fanIn[make_pair(edges[e].toBox, edges[e].toPort)]++;

            for (map<pair<int,int>,int>::iterator i = fanIn.begin();
                 i != fanIn.end(); ++i)
                if (i->second > 1)
                { printf("FAIL  %s: %d wires into one input (%s.%s)\n", argv[f],
                         i->second, boxes[i->first.first].name.c_str(),
                         boxes[i->first.first].ports[i->first.second].name.c_str());
                  problems++; }
        }

        /* no overlapping boxes */
        for (size_t a = 0; a < boxes.size() && problems < 5; a++)
            for (size_t b = a + 1; b < boxes.size(); b++)
                if (overlaps(boxes[a], boxes[b]))
                { printf("FAIL  %s: %s overlaps %s\n", argv[f],
                         boxes[a].name.c_str(), boxes[b].name.c_str());
                  problems++; break; }

        if (problems) failed++;
        else if (!quiet)
            printf("ok    %-34s %2d boxes %3d wires %2d layers%s\n",
                   argv[f], (int)boxes.size(), (int)edges.size(),
                   g.layerCount(),
                   g.feedbackCount() ? "  (has feedback)" : "");

        boxTotal += boxes.size();
        edgeTotal += edges.size();
        if (g.feedbackCount()) { fbFiles++; fbTotal += g.feedbackCount(); }
        if (g.layerCount() > maxLayers) maxLayers = g.layerCount();
        if ((int)boxes.size() > maxBoxes) maxBoxes = (int)boxes.size();
    }

    printf("\n%d graphs built, %d failed, %d skipped (would not load)\n",
           total, failed, skipped);
    if (total)
        printf("  %ld boxes, %ld wires; largest %d boxes, deepest %d layers\n"
               "  %ld feedback wires across %ld files\n",
               boxTotal, edgeTotal, maxBoxes, maxLayers, fbTotal, fbFiles);

    return failed;
}
