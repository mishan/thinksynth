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
 *   - a probe panel armed on any output port sits above its host at the height
 *     its visual module asked for, overlaps nothing, hit-tests where it is
 *     drawn, cannot be dragged off, is idempotent to arm twice, and leaves no
 *     trace when removed
 *   - a shared control -- one several nodes read -- is laid out before
 *     everything it drives, rather than being stranded in layer 0
 *   - clicking the middle of a wire finds a wire, and the ends of every
 *     wire sit on the ports it claims to join
 *   - a saved layout comes back exactly, and saving changes nothing in the
 *     file except `# @layout' lines
 *   - probes survive the same round trip through `# @probe' lines, in order,
 *     and a graph carrying none writes none -- otherwise every save of every
 *     patch would start growing a block
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
#include <set>

static bool overlaps (const NodeGraph::Box &a, const NodeGraph::Box &b)
{
    return !(a.x + a.w <= b.x || b.x + b.w <= a.x ||
             a.y + a.h <= b.y || b.y + b.h <= a.y);
}

/* Every line of a file except the ones the editor's block owns. Two files
   agreeing on this are the same file as far as the parser is concerned.
 *
 * Both prefixes, because probes share the block. Leaving `# @probe' out of the
 * strip would make the "writing the layout changed N other lines" check below
 * fire on every file that has one -- correctly, since they would then be
 * accumulating a line per save, which is precisely the bug DSP_FORMAT.md
 * records the layout block already having had once. */
static bool nonLayoutLines (const string &path, vector<string> &out)
{
    ifstream in(path.c_str());

    if (!in)
        return false;

    string line;

    out.clear();

    while (getline(in, line))
        if (line.compare(0, 9, "# @layout") != 0 &&
            line.compare(0, 8, "# @probe") != 0)
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
    double wrapWidth = 0;
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
        else if (!strcmp(argv[i], "-w"))
        {
            /* Lay out wrapped into bands. Off in the shipped editor -- see
               NODE_EDITOR.md -- but the code exists, so the invariants have
               to hold under it too or it is untested code pretending
               otherwise. */
            if (++i >= argc) return 2;

            /* strtod with the end pointer checked, not atof. atof returns 0
               for anything it cannot read, and 0 is the value that means "do
               not wrap" -- so `-w 150O' with a letter O would have run the
               whole suite unwrapped and reported a clean pass for a mode it
               never exercised. A missing argument already fails hard here; a
               malformed one should too. */
            char *end = NULL;

            wrapWidth = strtod(argv[i], &end);

            if (end == argv[i] || *end != '\0' || wrapWidth < 0)
            {
                fprintf(stderr, "-w wants a width in pixels, not \"%s\"\n",
                        argv[i]);
                return 2;
            }
        }
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
    long probes = 0;
    long probeLines = 0;

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

        g.setWrapWidth(wrapWidth);
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

        /* a strip stays with its host, however the host got where it is
         *
         * Attached controls are stored as ordinary boxes but their position is
         * derived: layout() stacks them directly above the box they belong to,
         * and sitting there is most of what says which box that is. Nothing
         * enforced it afterwards, so dragging a node -- or NodeLayout::apply
         * restoring a saved position -- moved the host and left its strips
         * behind, over whatever happened to be underneath.
         *
         * Checked twice: as laid out, and again after moving every host, since
         * the first passes on code that never maintains the invariant. */
        for (int round = 0; round < 2 && problems < 5; round++)
        {
            if (round == 1)
                for (size_t b = 0; b < boxes.size(); b++)
                    if (boxes[b].attachedTo < 0)
                        g.moveBox((int)b, boxes[b].x + 37.0,
                                  boxes[b].y + 19.0);

            for (size_t b = 0; b < boxes.size() && problems < 5; b++)
            {
                const NodeGraph::Box &c = boxes[b];

                if (c.attachedTo < 0)
                    continue;

                if (c.attachedTo >= (int)boxes.size())
                { printf("FAIL  %s: %s attached to box %d, which does not "
                         "exist\n", argv[f], c.ctlArg.c_str(), c.attachedTo);
                  problems++; continue; }

                const NodeGraph::Box &host = boxes[c.attachedTo];

                if (c.x != host.x)
                { printf("FAIL  %s: strip %s at x=%.1f, host %s at x=%.1f%s\n",
                         argv[f], c.ctlArg.c_str(), c.x, host.name.c_str(),
                         host.x, round ? " (after moving the host)" : "");
                  problems++; }

                if (c.y + c.h > host.y)
                { printf("FAIL  %s: strip %s runs into host %s%s\n", argv[f],
                         c.ctlArg.c_str(), host.name.c_str(),
                         round ? " (after moving the host)" : "");
                  problems++; }
            }
        }

        /* Undo the shove so the checks below see the real layout. */
        g.layout();

        /* the io node's two halves: every port earned, every arg reachable
         *
         * The io node has no plugin to say which way its args face, so the
         * split between "audio out" and "midi in" is inferred. It was inferred
         * wrongly once -- the sink was given a port for every arg, so a patch
         * that parks its constants on the io node drew `res' and `waveform'
         * and `note' as things the speakers consume. 1000 of the 1347 sink
         * ports in this corpus were phantoms.
         *
         * Two properties catch a relapse. Every sink port is either something
         * the engine reads or something this file wires up -- no port without
         * a reason. And the io node's args are partitioned across the halves:
         * present exactly once between them, so the fix can neither lose an
         * arg nor show it twice. */
        {
            int sink = -1, source = -1;

            for (size_t b = 0; b < boxes.size(); b++)
            {
                if (boxes[b].isIoSink) sink = (int)b;
                if (boxes[b].isIoSource) source = (int)b;
            }

            if (sink >= 0)
            {
                set<int> fed;

                for (size_t e = 0; e < edges.size(); e++)
                    if (edges[e].toBox == sink)
                        fed.insert(edges[e].toPort);

                for (size_t k = 0; k < boxes[sink].ports.size(); k++)
                {
                    const string &nm = boxes[sink].ports[k].name;

                    if (NodeGraph::isIoEngineInput(nm) || fed.count((int)k))
                        continue;

                    printf("FAIL  %s: audio out has a port nothing feeds and "
                           "the engine never reads (%s)\n", argv[f], nm.c_str());
                    problems++;
                }
            }

            if (sink >= 0 && source >= 0)
            {
                map<string,int> seen;

                for (size_t k = 0; k < boxes[sink].params.size(); k++)
                    seen[boxes[sink].params[k].name]++;
                for (size_t k = 0; k < boxes[source].params.size(); k++)
                    seen[boxes[source].params[k].name]++;

                for (map<string,int>::iterator i = seen.begin();
                     i != seen.end(); ++i)
                    if (i->second != 1)
                    { printf("FAIL  %s: io arg %s appears on %d halves\n",
                             argv[f], i->first.c_str(), i->second);
                      problems++; }

                /* and a param sits with its port, so panel and canvas cannot
                   disagree about which way an arg faces */
                set<string> sinkPorts;

                for (size_t k = 0; k < boxes[sink].ports.size(); k++)
                    sinkPorts.insert(boxes[sink].ports[k].name);

                for (size_t k = 0; k < boxes[source].params.size(); k++)
                    if (sinkPorts.count(boxes[source].params[k].name))
                    { printf("FAIL  %s: io arg %s has a sink port but a source "
                             "param\n", argv[f],
                             boxes[source].params[k].name.c_str());
                      problems++; }
            }
        }

        /* rubber-band selection: the geometry, without a mouse
         *
         * Three properties. A band over the whole extent gathers every box
         * that can be gathered -- which is every one that is not an attached
         * strip, since a strip belongs to its host and is never selected
         * apart from it. A band snapped to one box gathers exactly that box.
         * And a band out beyond the extent gathers nothing, so a stray click
         * and drag on empty canvas clears rather than selects.
         *
         * The middle one is the property that actually bites: "touching, not
         * enclosing" is easy to write as "enclosing" by accident, and then
         * nothing at the edge of a wide patch can be caught without scrolling
         * off it. */
        {
            vector<int> got;
            int selectable = 0;

            for (size_t b = 0; b < boxes.size(); b++)
                if (boxes[b].attachedTo < 0)
                    selectable++;

            g.boxesIn(-1e6, -1e6, 1e6, 1e6, got);

            if ((int)got.size() != selectable)
            { printf("FAIL  %s: a band over everything took %d of %d boxes\n",
                     argv[f], (int)got.size(), selectable);
              problems++; }

            g.boxesIn(g.width() + 100, g.height() + 100,
                      g.width() + 200, g.height() + 200, got);

            if (!got.empty())
            { printf("FAIL  %s: a band off the end of the graph took %d "
                     "boxes\n", argv[f], (int)got.size());
              problems++; }

            for (size_t b = 0; b < boxes.size() && problems < 5; b++)
            {
                if (boxes[b].attachedTo >= 0)
                    continue;

                /* Just inside the box, so no neighbour is touched: boxes do
                   not overlap, which the check above has already run. */
                g.boxesIn(boxes[b].x + 1, boxes[b].y + 1,
                          boxes[b].x + boxes[b].w - 1,
                          boxes[b].y + boxes[b].h - 1, got);

                if (got.size() != 1 || got[0] != (int)b)
                { printf("FAIL  %s: a band on %s took %d boxes\n", argv[f],
                         boxes[b].name.c_str(), (int)got.size());
                  problems++; }
            }

            /* Corners in the wrong order mean the same rectangle. */
            vector<int> rev;

            g.boxesIn(1e6, 1e6, -1e6, -1e6, rev);

            if ((int)rev.size() != selectable)
            { printf("FAIL  %s: a band dragged up-left took %d, not %d\n",
                     argv[f], (int)rev.size(), selectable);
              problems++; }
        }

        /* a group drag is rigid, whichever box was grabbed
         *
         * Dragging several boxes into the top-left corner must not close the
         * arrangement up. Clamping each box on its own does exactly that --
         * the ones nearest the edge stop while the rest keep coming -- and
         * the damage depends on which box the pointer had hold of, so it is
         * easy to miss by testing with the leftmost one.
         *
         * Shoved far past the corner so the clamp certainly bites, then every
         * pairwise offset is compared with what it was. */
        {
            vector<int> all;

            g.boxesIn(-1e6, -1e6, 1e6, 1e6, all);

            if (all.size() > 1)
            {
                vector<double> ox, oy;

                for (size_t i = 0; i < all.size(); i++)
                {
                    ox.push_back(boxes[all[i]].x);
                    oy.push_back(boxes[all[i]].y);
                }

                g.moveSelection(all, -1e5, -1e5);

                double minX = boxes[all[0]].x, minY = boxes[all[0]].y;

                for (size_t i = 1; i < all.size(); i++)
                {
                    if (boxes[all[i]].x < minX) minX = boxes[all[i]].x;
                    if (boxes[all[i]].y < minY) minY = boxes[all[i]].y;
                }

                /* Hard against the edge, and not past it. */
                if (minX != 0 || minY != 0)
                { printf("FAIL  %s: a group shoved into the corner sits at "
                         "(%.1f,%.1f)\n", argv[f], minX, minY);
                  problems++; }

                for (size_t i = 0; i < all.size() && problems < 5; i++)
                {
                    const double gotX = boxes[all[i]].x - boxes[all[0]].x;
                    const double gotY = boxes[all[i]].y - boxes[all[0]].y;

                    if (gotX != ox[i] - ox[0] || gotY != oy[i] - oy[0])
                    { printf("FAIL  %s: %s moved relative to %s during a group "
                             "drag\n", argv[f], boxes[all[i]].name.c_str(),
                             boxes[all[0]].name.c_str());
                      problems++; }
                }

                g.layout();
            }
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

        /* probe panels.
         *
         * These are not in the .dsp -- the editor arms them -- so this arms
         * one on every output port of every node box and checks the panel
         * behaves like the attached control whose machinery it borrows: flush
         * against its host, overlapping nothing, hit-testable where it is
         * drawn, undraggable, and gone without trace when removed.
         *
         * The panel is deliberately taller than a control strip. The stack
         * used to be walked by multiplying by ATTACH_H, and with that multiply
         * restored the overlap check below fails on any node carrying both a
         * probe and a control -- which is what makes the height worth
         * asserting rather than assuming. */
        {
            const double PROBE_H = 64.0;

            for (size_t b = 0; b < g.boxes().size() && problems < 5; b++)
            {
                if (g.boxes()[b].isControl || g.boxes()[b].isProbe ||
                    g.boxes()[b].attachedTo >= 0)
                    continue;

                /* By name and index taken afresh each time: addProbe and
                   removeProbe both reshape boxes_, so a reference taken before
                   the loop would dangle. */
                const string hostName = g.boxes()[b].name;

                for (size_t p = 0; p < g.boxes()[b].ports.size() &&
                                   problems < 5; p++)
                {
                    if (g.boxes()[b].ports[p].isInput)
                        continue;

                    const size_t before = g.boxes().size();
                    const string port = g.boxes()[b].ports[p].name;

                    const int panel = g.addProbe((int)b, port, "meter",
                                                 PROBE_H);

                    if (panel < 0)
                    { printf("FAIL  %s: cannot probe %s.%s, an output port\n",
                             argv[f], hostName.c_str(), port.c_str());
                      problems++; continue; }

                    /* Twice is once: two panels on one signal is never what
                       was meant, and a menu makes it easy to ask for. */
                    if (g.addProbe((int)b, port, "meter", PROBE_H) != panel)
                    { printf("FAIL  %s: probing %s.%s twice made two panels\n",
                             argv[f], hostName.c_str(), port.c_str());
                      problems++; }

                    g.layout();

                    {
                        const vector<NodeGraph::Box> &nb = g.boxes();
                        const NodeGraph::Box &pnl = nb[panel];

                        /* Checked rather than assumed: layout() reassigns
                           attachments, and an earlier version of
                           assignAttachments cleared every box's host before
                           reassigning -- which detached each panel and turned
                           this into a subscript of -1. A harness that
                           segfaults instead of saying what went wrong is a
                           worse harness. */
                        if (pnl.attachedTo != (int)b)
                        { printf("FAIL  %s: panel on %s.%s came back attached "
                                 "to %d, not %d\n", argv[f], hostName.c_str(),
                                 port.c_str(), pnl.attachedTo, (int)b);
                          problems++;
                          g.removeProbe(panel);
                          g.layout();
                          continue; }

                        const NodeGraph::Box &host = nb[pnl.attachedTo];

                        if (pnl.x != host.x)
                        { printf("FAIL  %s: panel on %s.%s at x=%.1f, host at "
                                 "%.1f\n", argv[f], hostName.c_str(),
                                 port.c_str(), pnl.x, host.x);
                          problems++; }

                        if (pnl.y + pnl.h > host.y)
                        { printf("FAIL  %s: panel on %s.%s runs into its "
                                 "host\n", argv[f], hostName.c_str(),
                                 port.c_str());
                          problems++; }

                        if (pnl.h != PROBE_H)
                        { printf("FAIL  %s: panel on %s.%s is %.1f tall, "
                                 "asked for %.1f\n", argv[f], hostName.c_str(),
                                 port.c_str(), pnl.h, PROBE_H);
                          problems++; }

                        for (size_t a = 0; a < nb.size() && problems < 5; a++)
                            if (a != (size_t)panel && overlaps(nb[a], pnl))
                            { printf("FAIL  %s: panel on %s.%s overlaps %s\n",
                                     argv[f], hostName.c_str(), port.c_str(),
                                     nb[a].name.c_str());
                              problems++; break; }

                        if (g.boxAt(pnl.x + pnl.w / 2,
                                    pnl.y + pnl.h / 2) != panel)
                        { printf("FAIL  %s: the middle of the panel on %s.%s "
                                 "does not pick it\n", argv[f],
                                 hostName.c_str(), port.c_str());
                          problems++; }

                        /* A panel is not a node: it must not be draggable off
                           its host. Enforced by attachedTo rather than by
                           anything probe-specific, which is why it is worth
                           checking from this side too. */
                        const double px = pnl.x, py = pnl.y;

                        g.moveBox(panel, px + 50, py + 50);

                        if (g.boxes()[panel].x != px ||
                            g.boxes()[panel].y != py)
                        { printf("FAIL  %s: the panel on %s.%s can be dragged "
                                 "off its host\n", argv[f], hostName.c_str(),
                                 port.c_str());
                          problems++; }
                    }

                    g.removeProbe(panel);
                    g.layout();

                    if (g.boxes().size() != before)
                    { printf("FAIL  %s: removing the panel on %s.%s left %d "
                             "boxes, not %d\n", argv[f], hostName.c_str(),
                             port.c_str(), (int)g.boxes().size(),
                             (int)before);
                      problems++; }

                    probes++;
                }
            }

            /* (node name, output port) has to name exactly one box.
             *
             * This is the editor's lookup, checked here because it is the one
             * piece of NodeEditor::reapplyProbes that can go quietly wrong.
             * A probe is stored by node and arg name -- that is what survives
             * a reload and what a `# @probe' line records -- and putting the
             * panel back means finding the box again. But the io node is one
             * node in the file and *two* boxes on screen, so a search by name
             * alone has two candidates and would take whichever came first.
             * Requiring the port narrows it to one; this asserts that it
             * really does, for every point that can be probed. */
            for (size_t b = 0; b < g.boxes().size() && problems < 5; b++)
            {
                if (g.boxes()[b].isControl || g.boxes()[b].isProbe)
                    continue;

                for (size_t p = 0; p < g.boxes()[b].ports.size() &&
                                   problems < 5; p++)
                {
                    if (g.boxes()[b].ports[p].isInput)
                        continue;

                    const string node = g.boxes()[b].name;
                    const string port = g.boxes()[b].ports[p].name;

                    int found = 0;

                    for (size_t k = 0; k < g.boxes().size(); k++)
                    {
                        if (g.boxes()[k].isControl || g.boxes()[k].isProbe ||
                            g.boxes()[k].name != node)
                            continue;

                        for (size_t q = 0; q < g.boxes()[k].ports.size(); q++)
                            if (!g.boxes()[k].ports[q].isInput &&
                                g.boxes()[k].ports[q].name == port)
                                found++;
                    }

                    if (found != 1)
                    { printf("FAIL  %s: %s.%s names %d boxes, not one -- a "
                             "probe could not be put back on the right one\n",
                             argv[f], node.c_str(), port.c_str(), found);
                      problems++; }
                }
            }

            /* Nothing above may have disturbed the graph the checks below
               measure. */
            g.layout();
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

            /* Against the range the track is *drawn* over, which is what the
               file declares except where the control's values are named -- and
               there the list is the range. Eight shipped patches declare a
               maximum of 5.1 or 5.5 for six waveforms, padding for a slider
               that could not otherwise land on the last one.

               Box keeps both: the declared numbers stay untouched because
               NodeEdit writes them back into the file, so asking bx.ctlMax here
               would be asking the wrong one. */
            const double lo = bx.ctlDrawMin(), hi = bx.ctlDrawMax();

            /* The track is only about a hundred pixels wide, so a value
               recovered from a handle position is quantised to roughly a
               hundredth of the range. */
            const double v = g.sliderValueAt((int)b, hx);
            const double tol = (hi - lo) * 0.02 + 1e-6;

            if (fabs(v - (double)bx.ctlValue) > tol)
            { printf("FAIL  %s: @%s reads %g at its own handle, not %g\n",
                     argv[f], bx.ctlArg.c_str(), v, (double)bx.ctlValue);
              problems++; continue; }

            /* Both ends of the track must give the limits it is drawn over. */
            if (fabs(g.sliderValueAt((int)b, x0) - lo) > tol ||
                fabs(g.sliderValueAt((int)b, x1) - hi) > tol)
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

        /* probe round-trip, on a copy so the corpus is never touched.
         *
         * The properties are the layout block's, because probes live in it and
         * inherit its hazards: a save must not disturb any other line, saving
         * twice must equal saving once, and what comes back must be what went
         * in. Plus one of its own -- a probe carries no position, so what has
         * to survive is the (node, arg, visual) triple and the order.
         *
         * The no-op case is checked separately and is the one that matters
         * most: a file with no probes must come back with none, or every save
         * of every patch in the corpus would start growing a block. */
        if (problems == 0)
        {
            const string tmp = "/tmp/dspgraph-probe.dsp";

            vector<string> before, after;

            if (!copyFile(argv[f], tmp) || !nonLayoutLines(tmp, before))
            { printf("FAIL  %s: could not copy for the probe round-trip\n",
                     argv[f]);
              problems++; }
            else
            {
                NodeGraph pg;

                pg.build(tree);

                /* One panel on every output port of the first three node
                   boxes: enough to cover several on one host, several hosts,
                   and the ordering between them, without writing a block
                   longer than some of the files. */
                vector<string> wantNode, wantArg;
                int hosts = 0;

                for (size_t b = 0; b < pg.boxes().size() && hosts < 3; b++)
                {
                    if (pg.boxes()[b].isControl || pg.boxes()[b].isProbe)
                        continue;

                    bool any = false;

                    for (size_t k = 0; k < pg.boxes()[b].ports.size(); k++)
                    {
                        if (pg.boxes()[b].ports[k].isInput)
                            continue;

                        const string node = pg.boxes()[b].name;
                        const string arg = pg.boxes()[b].ports[k].name;

                        if (pg.addProbe((int)b, arg, "meter", 36.0) < 0)
                            continue;

                        wantNode.push_back(node);
                        wantArg.push_back(arg);
                        any = true;
                    }

                    if (any)
                        hosts++;
                }

                pg.layout();

                vector<NodeLayout::ProbeRef> got;

                if (!NodeLayout::write(tmp, pg))
                { printf("FAIL  %s: could not write the probe block\n",
                         argv[f]);
                  problems++; }
                else if (!nonLayoutLines(tmp, after) || before != after)
                { printf("FAIL  %s: writing probes changed %d other line(s)\n",
                         argv[f], (int)after.size() - (int)before.size());
                  problems++; }
                else if (!NodeLayout::write(tmp, pg) ||
                         !nonLayoutLines(tmp, after) || before != after)
                { printf("FAIL  %s: saving probes twice differs from once\n",
                         argv[f]);
                  problems++; }
                else if (!NodeLayout::readProbes(tmp, got))
                { printf("FAIL  %s: could not read the probe block back\n",
                         argv[f]);
                  problems++; }
                else if (got.size() != wantNode.size())
                { printf("FAIL  %s: wrote %d probes, read back %d\n", argv[f],
                         (int)wantNode.size(), (int)got.size());
                  problems++; }
                else
                {
                    for (size_t i = 0; i < got.size() && problems < 5; i++)
                        if (got[i].node != wantNode[i] ||
                            got[i].arg != wantArg[i] ||
                            got[i].visual != "meter")
                        { printf("FAIL  %s: probe %d came back as %s.%s/%s, "
                                 "not %s.%s/meter\n", argv[f], (int)i,
                                 got[i].node.c_str(), got[i].arg.c_str(),
                                 got[i].visual.c_str(), wantNode[i].c_str(),
                                 wantArg[i].c_str());
                          problems++; }

                    probeLines += (long)got.size();

                    /* And a graph with no probes writes no probe lines. */
                    NodeGraph clean;

                    clean.build(tree);
                    clean.layout();

                    vector<NodeLayout::ProbeRef> none;

                    if (!NodeLayout::write(tmp, clean) ||
                        !NodeLayout::readProbes(tmp, none) || !none.empty())
                    { printf("FAIL  %s: a graph with no probes wrote %d probe "
                             "line(s)\n", argv[f], (int)none.size());
                      problems++; }
                }

                remove(tmp.c_str());
            }
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

                    /* Attached controls are not among them: a strip's
                       position comes from its host every time, so it is
                       neither written nor restored. Counting them would demand
                       a saved position for something that deliberately has
                       none. */
                    int expect = 0;

                    for (size_t b = 0; b < boxes.size(); b++)
                        if (boxes[b].attachedTo < 0)
                            expect++;

                    if (applied != expect)
                    { printf("FAIL  %s: restored %d of %d positions\n", argv[f],
                             applied, expect);
                      problems++; }
                    else
                        for (size_t b = 0; b < boxes.size(); b++)
                        {
                            const NodeGraph::Box &r = g2.boxes()[b];

                            /* A strip has to come back on its host, wherever
                               apply() put that host -- the invariant survives
                               a save and a reload, not just a fresh layout. */
                            if (r.attachedTo >= 0)
                            {
                                const NodeGraph::Box &h =
                                    g2.boxes()[r.attachedTo];

                                if (r.x != h.x || r.y + r.h > h.y)
                                { printf("FAIL  %s: strip %s did not come back "
                                         "on %s\n", argv[f], r.ctlArg.c_str(),
                                         h.name.c_str());
                                  problems++; break; }

                                continue;
                            }

                            if (r.x != 17.0 + b * 13.0 ||
                                r.y != 23.0 + b * 7.0)
                            { printf("FAIL  %s: %s came back at (%.1f,%.1f)\n",
                                     argv[f], boxes[b].name.c_str(),
                                     r.x, r.y);
                              problems++; break; }
                        }
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
    if (total)
        printf("  %ld probe panels armed and removed, one per output port\n",
               probes);
    if (total)
        printf("  %ld probes written and read back, and every file still "
               "writes none when it has none\n", probeLines);

    return failed;
}
