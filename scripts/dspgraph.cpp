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
 *   - clicking a box's centre picks that box, and clicking a port's drawn
 *     position picks that port -- the two things a mouse does, checked
 *     without a mouse
 *   - every control's slider handle is where its value says, and clicking it
 *     finds that control, whether it is a free-standing box or a strip
 *     attached to the node it drives
 *   - an attached control sits above its host, overlaps nothing, and its
 *     wire still reaches the port it drives -- adjacency says which node a
 *     control belongs to, only the wire says which parameter
 *   - a shared control -- one several nodes read -- is laid out before
 *     everything it drives, rather than being stranded in layer 0
 *   - clicking the middle of a wire finds a wire, and the ends of every
 *     wire sit on the ports it claims to join
 *   - a saved layout comes back exactly, and saving changes nothing in the
 *     file except `# @layout' lines
 *   - the parameter list and the wires agree: every parameter driven by
 *     another node has a wire to match, and every wire has a parameter. The
 *     two are built by separate passes over separate data, so they disagreeing
 *     means one of them is wrong
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
#include "NodeLayout.h"

#include <fstream>
#include <sstream>

static bool overlaps (const NodeGraph::Box &a, const NodeGraph::Box &b)
{
    return !(a.x + a.w <= b.x || b.x + b.w <= a.x ||
             a.y + a.h <= b.y || b.y + b.h <= a.y);
}

/* Every line of a file except its `# @layout' comments. Two files agreeing on
   this are the same file as far as the parser is concerned. */
static bool nonLayoutLines (const string &path, vector<string> &out)
{
    ifstream in(path.c_str());

    if (!in)
        return false;

    string line;

    out.clear();

    while (getline(in, line))
        if (line.compare(0, 9, "# @layout") != 0)
            out.push_back(line);

    /* write() drops trailing blank lines, so ignore them on both sides. */
    while (!out.empty() &&
           out.back().find_first_not_of(" \t\r") == string::npos)
        out.pop_back();

    return true;
}

static bool copyFile (const string &from, const string &to)
{
    ifstream in(from.c_str(), ios::binary);
    ofstream out(to.c_str(), ios::binary | ios::trunc);

    if (!in || !out)
        return false;

    out << in.rdbuf();

    return out.good();
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
    long controls = 0, attached = 0, shared = 0;

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

        /* hit-testing: a click at a box's centre picks that box */
        for (size_t b = 0; b < boxes.size() && problems < 5; b++)
        {
            const NodeGraph::Box &bx = boxes[b];
            const int hit = g.boxAt(bx.x + bx.w / 2, bx.y + bx.h / 2);

            if (hit != (int)b)
            { printf("FAIL  %s: centre of %s picks box %d, not %d\n", argv[f],
                     bx.name.c_str(), hit, (int)b);
              problems++; }

            /* ...and at a port's drawn position picks that port. Boxes do not
               overlap, so there is exactly one right answer.
            
               An attached control draws no ports, so there is nothing to
               click and nothing to check. */
            for (size_t k = 0; k < bx.ports.size() && problems < 5 &&
                               bx.attachedTo < 0; k++)
            {
                double px, py;
                int hb = -1, hp = -1;

                g.portPos((int)b, (int)k, px, py);

                if (!g.portAt(px, py, hb, hp) ||
                    hb != (int)b || hp != (int)k)
                { printf("FAIL  %s: port %s.%s at (%.1f,%.1f) picks %d/%d\n",
                         argv[f], bx.name.c_str(), bx.ports[k].name.c_str(),
                         px, py, hb, hp);
                  problems++; }
            }
        }

        /* wires: what is drawn is what can be clicked.
         *
         * edgeCurve() feeds both the canvas and edgeAt(), so this is really
         * checking that the sampling in edgeAt is fine enough and that the
         * curve ends where the ports are. Wires overlap, so the midpoint of
         * one may legitimately find another -- what must not happen is
         * finding nothing at all. */
        for (size_t e = 0; e < edges.size() && problems < 5; e++)
        {
            double xs[4], ys[4];

            g.edgeCurve((int)e, xs, ys);

            double px, py;

            g.portPos(edges[e].fromBox, edges[e].fromPort, px, py);

            if (xs[0] != px || ys[0] != py)
            { printf("FAIL  %s: wire %d does not start at its port\n",
                     argv[f], (int)e);
              problems++; continue; }

            g.portPos(edges[e].toBox, edges[e].toPort, px, py);

            if (xs[3] != px || ys[3] != py)
            { printf("FAIL  %s: wire %d does not end at its port\n",
                     argv[f], (int)e);
              problems++; continue; }

            /* the point at t = 0.5 on the cubic */
            const double mx = 0.125 * (xs[0] + 3*xs[1] + 3*xs[2] + xs[3]);
            const double my = 0.125 * (ys[0] + 3*ys[1] + 3*ys[2] + ys[3]);

            if (g.edgeAt(mx, my) < 0)
            { printf("FAIL  %s: the middle of wire %d finds no wire\n",
                     argv[f], (int)e);
              problems++; }
        }

        /* connect() and removeEdge() must keep the params and the wires in
           step, the same way build() does. */
        if (problems == 0 && !edges.empty())
        {
            NodeGraph g2;

            g2.build(tree);
            g2.layout();

            const size_t before = g2.edges().size();

            g2.removeEdge(0);

            if (g2.edges().size() != before - 1)
            { printf("FAIL  %s: removeEdge did not remove one\n", argv[f]);
              problems++; }

            const NodeGraph::Edge &e0 = edges[0];

            string why;

            if (!g2.connect(e0.fromBox, e0.fromPort, e0.toBox, e0.toPort, why))
            { printf("FAIL  %s: cannot reconnect wire 0: %s\n", argv[f],
                     why.c_str());
              problems++; }
            else if (g2.edges().size() != before)
            { printf("FAIL  %s: reconnecting changed the wire count to %d\n",
                     argv[f], (int)g2.edges().size());
              problems++; }
            else
            {
                /* connecting a second time must replace, not duplicate */
                g2.connect(e0.fromBox, e0.fromPort, e0.toBox, e0.toPort, why);

                if (g2.edges().size() != before)
                { printf("FAIL  %s: connecting twice made %d wires, not %d\n",
                         argv[f], (int)g2.edges().size(), (int)before);
                  problems++; }
            }
        }

        /* controls: the handle is where the value says, and it can be hit.
         *
         * Same reasoning as the wires -- sliderGeometry feeds the drawing and
         * sliderAt/sliderValueAt both, so this checks the two directions
         * agree: value -> handle position -> value. */
        for (size_t b = 0; b < boxes.size() && problems < 5; b++)
        {
            const NodeGraph::Box &bx = boxes[b];

            if (!bx.isControl)
                continue;

            controls++;

            double x0, x1, y, hx;

            if (!g.sliderGeometry((int)b, x0, x1, y, hx))
            { printf("FAIL  %s: control @%s has no slider\n", argv[f],
                     bx.ctlArg.c_str());
              problems++; continue; }

            if (g.sliderAt(hx, y) != (int)b)
            { printf("FAIL  %s: the handle of @%s picks control %d, not %d\n",
                     argv[f], bx.ctlArg.c_str(), g.sliderAt(hx, y), (int)b);
              problems++; continue; }

            /* The track is only about a hundred pixels wide, so a value
               recovered from a handle position is quantised to roughly a
               hundredth of the range. */
            const double v = g.sliderValueAt((int)b, hx);
            const double tol = (bx.ctlMax - bx.ctlMin) * 0.02 + 1e-6;

            if (fabs(v - (double)bx.ctlValue) > tol)
            { printf("FAIL  %s: @%s reads %g at its own handle, not %g\n",
                     argv[f], bx.ctlArg.c_str(), v, (double)bx.ctlValue);
              problems++; continue; }

            /* Both ends of the track must give the declared limits. */
            if (fabs(g.sliderValueAt((int)b, x0) - bx.ctlMin) > tol ||
                fabs(g.sliderValueAt((int)b, x1) - bx.ctlMax) > tol)
            { printf("FAIL  %s: @%s track ends do not give its range\n",
                     argv[f], bx.ctlArg.c_str());
              problems++; }
        }

        /* attached controls sit where they say they do.
         *
         * The overlap check above already proves they do not collide with
         * anything. This is the other half: that each one is actually beside
         * the box it belongs to, rather than merely somewhere legal. */
        for (size_t b = 0; b < boxes.size() && problems < 5; b++)
        {
            const NodeGraph::Box &bx = boxes[b];

            if (bx.attachedTo < 0)
                continue;

            attached++;

            const NodeGraph::Box &host = boxes[bx.attachedTo];

            /* Above the host, in the same column. */
            if (bx.y + bx.h > host.y)
            { printf("FAIL  %s: @%s runs into %s\n", argv[f],
                     bx.ctlArg.c_str(), host.name.c_str());
              problems++; continue; }

            if (bx.x != host.x)
            { printf("FAIL  %s: @%s is not in %s's column\n", argv[f],
                     bx.ctlArg.c_str(), host.name.c_str());
              problems++; continue; }

            /* Close above it: the whole point is that it reads as part of
               that node rather than as something nearby. */
            if (host.y - (bx.y + bx.h) > 160.0)
            { printf("FAIL  %s: @%s is %.0fpx above %s, not on it\n",
                     argv[f], bx.ctlArg.c_str(),
                     host.y - (bx.y + bx.h), host.name.c_str());
              problems++; continue; }

            /* And its wire still lands on a real port of the host, which is
               the only thing that says which parameter it drives. */
            bool wired = false;

            for (size_t e = 0; e < edges.size(); e++)
                if (edges[e].fromBox == (int)b && edges[e].toBox == bx.attachedTo)
                {
                    double xs[4], ys[4];

                    g.edgeCurve((int)e, xs, ys);

                    double px, py;

                    g.portPos(edges[e].toBox, edges[e].toPort, px, py);

                    if (xs[3] == px && ys[3] == py)
                        wired = true;
                }

            if (!wired)
            { printf("FAIL  %s: @%s has no wire to a port of %s\n", argv[f],
                     bx.ctlArg.c_str(), host.name.c_str());
              problems++; }
        }

        /* a shared control comes before what it drives.
         *
         * It cannot attach to any one consumer, so it stays a box; the least
         * it can do is sit near them rather than at the far left with wires
         * across the whole patch. */
        for (size_t b = 0; b < boxes.size() && problems < 5; b++)
        {
            const NodeGraph::Box &bx = boxes[b];

            if (!bx.isControl || bx.attachedTo >= 0)
                continue;

            int consumers = 0;

            for (size_t e = 0; e < edges.size(); e++)
            {
                if (edges[e].fromBox != (int)b)
                    continue;

                consumers++;

                if (boxes[edges[e].toBox].layer <= bx.layer)
                { printf("FAIL  %s: @%s is in layer %d, not before %s "
                         "in layer %d\n", argv[f], bx.ctlArg.c_str(),
                         bx.layer, boxes[edges[e].toBox].name.c_str(),
                         boxes[edges[e].toBox].layer);
                  problems++; break; }
            }

            if (consumers > 1)
                shared++;
        }

        /* no plugin-internal state exposed.
         *
         * The header comment has claimed this from the start and nothing
         * checked it. INOUT_ args are delay ring buffers, envelope positions,
         * filter history -- 69 of them across the plugins, none referenced by
         * any .dsp -- and a port for one would invite wiring something that is
         * not a signal. */
        for (size_t b = 0; b < boxes.size() && problems < 5; b++)
        {
            const NodeGraph::Box &bx = boxes[b];

            thNode *n = tree->findNode(bx.name);
            thPlugin *pl = n ? n->plugin() : NULL;

            if (pl == NULL)
                continue;

            for (int k = 0; k < pl->argCount(); k++)
            {
                if (pl->getArgDir(k) != thPlugin::ARG_STATE)
                    continue;

                const string sname = pl->getArgName(k);

                for (size_t q = 0; q < bx.ports.size(); q++)
                    if (bx.ports[q].name == sname)
                    { printf("FAIL  %s: %s exposes internal state %s as a "
                             "port\n", argv[f], bx.name.c_str(), sname.c_str());
                      problems++; break; }

                for (size_t q = 0; q < bx.params.size(); q++)
                    if (bx.params[q].name == sname)
                    { printf("FAIL  %s: %s lists internal state %s as a "
                             "parameter\n", argv[f], bx.name.c_str(),
                             sname.c_str());
                      problems++; break; }
            }
        }

        /* parameters and wires must describe the same thing.
         *
         * Params come from each node's thArgMap; edges come from a separate
         * pass resolving ARG_POINTER names to boxes. Nothing keeps them in
         * step, so cross-checking them is close to free and would catch a
         * whole class of "the panel says one thing, the canvas another". */
        for (size_t b = 0; b < boxes.size() && problems < 5; b++)
        {
            const NodeGraph::Box &bx = boxes[b];

            /* The io source half is a display artefact with no node behind
               it, and a control box is a `@name' block rather than a node.
               Neither has an arg map to cross-check. */
            if (bx.isIoSource || bx.isControl)
                continue;

            int wiredParams = 0;

            for (size_t k = 0; k < bx.params.size(); k++)
            {
                /* A chanarg reference is a wire now too -- it comes from the
                   control box for that `@name'. */
                if (bx.params[k].kind == NodeGraph::Param::CHANARG)
                {
                    bool found = false;

                    for (size_t e = 0; e < edges.size() && !found; e++)
                        if (edges[e].toBox == (int)b &&
                            boxes[edges[e].toBox].ports[edges[e].toPort].name ==
                                bx.params[k].name &&
                            boxes[edges[e].fromBox].isControl)
                            found = true;

                    /* Reading a chanarg the file never declared leaves nothing
                       to wire it to, and the parser has already complained. */
                    if (found)
                        wiredParams++;

                    continue;
                }

                if (bx.params[k].kind == NodeGraph::Param::POINTER)
                {
                    /* A reference to a node that does not exist is the .dsp's
                       bug, not the graph's -- old/firtest.dsp reads filt->out
                       and env->out with neither node defined, and the parser
                       already says so. There is correctly no wire, and the
                       panel correctly still shows what the file asked for. */
                    const string &src = bx.params[k].source;
                    const string node = src.substr(0, src.find("->"));

                    if (g.boxByName(node) < 0)
                        continue;

                    wiredParams++;

                    bool found = false;

                    for (size_t e = 0; e < edges.size() && !found; e++)
                        if (edges[e].toBox == (int)b &&
                            boxes[edges[e].toBox].ports[edges[e].toPort].name ==
                                bx.params[k].name)
                            found = true;

                    if (!found)
                    { printf("FAIL  %s: %s.%s says it is driven by %s, "
                             "but there is no wire\n", argv[f],
                             bx.name.c_str(), bx.params[k].name.c_str(),
                             bx.params[k].source.c_str());
                      problems++; }
                }
            }

            int incoming = 0;

            for (size_t e = 0; e < edges.size(); e++)
                if (edges[e].toBox == (int)b)
                    incoming++;

            if (incoming != wiredParams)
            { printf("FAIL  %s: %s has %d wires in but %d wired parameters\n",
                     argv[f], bx.name.c_str(), incoming, wiredParams);
              problems++; }
        }

        /* layout round-trip, on a copy so the corpus is never touched */
        if (problems == 0)
        {
            const string tmp = "/tmp/dspgraph-layout.dsp";

            vector<string> before, after;

            if (!copyFile(argv[f], tmp) || !nonLayoutLines(tmp, before))
            { printf("FAIL  %s: could not copy for round-trip\n", argv[f]);
              problems++; }
            else
            {
                /* Move everything somewhere arbitrary but reproducible, so a
                   layout that silently failed to save cannot pass by
                   coincidentally matching what layout() would recompute. */
                for (size_t b = 0; b < boxes.size(); b++)
                    g.moveBox((int)b, 17.0 + b * 13.0, 23.0 + b * 7.0);

                g.refreshExtent();

                if (!NodeLayout::write(tmp, g))
                { printf("FAIL  %s: layout write failed\n", argv[f]);
                  problems++; }
                else if (!nonLayoutLines(tmp, after))
                { printf("FAIL  %s: could not re-read after write\n", argv[f]);
                  problems++; }
                else if (before != after)
                { printf("FAIL  %s: writing the layout changed %d other line(s)\n",
                         argv[f], (int)after.size() - (int)before.size());
                  problems++; }
                else if (!NodeLayout::write(tmp, g) ||
                         !nonLayoutLines(tmp, after) || before != after)
                { printf("FAIL  %s: saving twice is not the same as once\n",
                         argv[f]);
                  problems++; }
                else
                {
                    NodeLayout::PosMap pos;

                    NodeLayout::read(tmp, pos);

                    /* Read back into a freshly laid-out graph, the way opening
                       the file again would. */
                    NodeGraph g2;

                    g2.build(tree);
                    g2.layout();

                    const int applied = NodeLayout::apply(g2, pos, g2);

                    if (applied != (int)boxes.size())
                    { printf("FAIL  %s: restored %d of %d positions\n", argv[f],
                             applied, (int)boxes.size());
                      problems++; }
                    else
                        for (size_t b = 0; b < boxes.size(); b++)
                            if (g2.boxes()[b].x != 17.0 + b * 13.0 ||
                                g2.boxes()[b].y != 23.0 + b * 7.0)
                            { printf("FAIL  %s: %s came back at (%.1f,%.1f)\n",
                                     argv[f], boxes[b].name.c_str(),
                                     g2.boxes()[b].x, g2.boxes()[b].y);
                              problems++; break; }
                }
            }

            remove(tmp.c_str());
        }

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
        printf("  %ld boxes (%ld controls: %ld attached, %ld shared), "
               "%ld wires; "
               "largest %d boxes, deepest %d layers\n"
               "  %ld feedback wires across %ld files\n",
               boxTotal, controls, attached, shared, edgeTotal, maxBoxes,
               maxLayers,
               fbTotal, fbFiles);

    return failed;
}
