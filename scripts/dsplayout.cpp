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
 * dsplayout -- what a graph's shape costs, and what could be done about it.
 *
 * Reports, per file:
 *
 *   - the drawn size, and the layer count
 *   - whether that layer count is the minimum possible. Longest-path layering
 *     puts every node at its critical-path depth, and no left-to-right
 *     drawing can have fewer columns than that, so this is a check that the
 *     layering is optimal rather than a knob to turn
 *   - the number of wires crossing each layer boundary. That is what wrapping
 *     a wide graph into stacked bands would cost: every wire across the split
 *     becomes a long return from the right edge to the left, one band down
 *
 * Written to answer "why not use a different layering algorithm to make wide
 * graphs narrower". The answer is in the numbers: the layering is already
 * optimal, and the cut profile says wrapping would trade a scrollbar for a
 * tangle. See NODE_EDITOR.md, "Why the layout will not get much narrower".
 *
 *   make -C scripts
 *   LD_LIBRARY_PATH=libthink scripts/dsplayout -p plugins/ dsp/ts1.dsp
 */

#include "config.h"
#include <stdio.h>
#include <vector>
#include <string.h>
#include "think.h"
#include "NodeGraph.h"
/* The least number of columns any left-to-right drawing could have. */
static int criticalPath (const NodeGraph &g)
{
    const int n = (int)g.boxes().size();

    std::vector<int> depth(n, 0);

    for (int pass = 0; pass < n + 1; pass++)
    {
        bool changed = false;

        for (size_t e = 0; e < g.edges().size(); e++)
        {
            const NodeGraph::Edge &ed = g.edges()[e];

            /* Both ends bounds-checked before indexing. An Edge's indices
               start at -1 and are filled in later, so a partially built one
               can exist, and a measurement harness is the last thing that
               should segfault rather than report. */
            if (ed.fromBox < 0 || ed.fromBox >= n ||
                ed.toBox < 0 || ed.toBox >= n)
                continue;

            if (ed.feedback || g.boxes()[ed.fromBox].attachedTo >= 0)
                continue;

            if (depth[ed.toBox] < depth[ed.fromBox] + 1)
            {
                depth[ed.toBox] = depth[ed.fromBox] + 1;
                changed = true;
            }
        }

        if (!changed)
            break;
    }

    int cp = 0;

    for (int i = 0; i < n; i++)
        if (g.boxes()[i].attachedTo < 0 && depth[i] > cp)
            cp = depth[i];

    return cp + 1;
}

int main (int argc, char **argv)
{
    string pluginPath = PLUGIN_PATH;
    int first = -1;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-p")) { if (++i >= argc) return 2; pluginPath = argv[i]; }
        else { first = i; break; }
    }

    if (first < 0)
    { printf("usage: %s [-p PATH] file.dsp ...\n", argv[0]); return 2; }

    int suboptimal = 0, n = 0;
    long worstCut = 0;

    for (int f = first; f < argc; f++)
    {
        thSynth s(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

        thSynthTree *t = s.parseTree(argv[f]);

        if (t == NULL)
            continue;

        NodeGraph g;

        g.build(t);
        g.layout();
        delete t;

        n++;

        const int cp = criticalPath(g);

        if (g.layerCount() > cp)
            suboptimal++;

        printf("%-24s %4.0f x %4.0f  %2d layers (min %2d)  cuts:",
               argv[f], g.width(), g.height(), g.layerCount(), cp);

        int best = 1 << 30, bestAt = -1, mid = 0;

        for (int k = 1; k < g.layerCount(); k++)
        {
            int c = 0;

            for (size_t e = 0; e < g.edges().size(); e++)
            {
                const NodeGraph::Edge &ed = g.edges()[e];

                if (ed.fromBox < 0 || ed.fromBox >= (int)g.boxes().size() ||
                    ed.toBox < 0 || ed.toBox >= (int)g.boxes().size())
                    continue;

                if (g.boxes()[ed.fromBox].attachedTo >= 0)
                    continue;

                if (g.boxes()[ed.fromBox].layer < k &&
                    g.boxes()[ed.toBox].layer >= k)
                    c++;
            }

            printf(" %d", c);

            if (c < best) { best = c; bestAt = k; }

            /* What a two-band wrap would actually cost: the split has to be
               near the middle to halve the width, not wherever the graph
               happens to be thinnest. */
            if (k == g.layerCount() / 2)
                mid = c;
        }

        if (mid > worstCut)
            worstCut = mid;

        printf("   min %d at %d, middle %d\n", best, bestAt, mid);
    }

    printf("\n%d graphs, %d above the minimum layer count\n", n, suboptimal);
    printf("worst mid-split cut: %ld wires -- that many long returns is what "
           "wrapping would cost\n", worstCut);

    return 0;
}
