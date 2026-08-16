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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fstream>
#include <sstream>
#include <vector>

#include "think.h"
#include "NodeGraph.h"
#include "NodeLayout.h"

/* Every line the block owns starts with this; the entries add a space and a
   node name. Anchoring the *whole* block on one prefix -- prose included --
   is what makes a save idempotent: a header comment that did not match would
   survive the strip and be written again, gaining two lines per save. */
#define LAYOUT_PFX  "# @layout"
#define LAYOUT_TAG  LAYOUT_PFX " "

/* The probes share the block and the discipline: stripped on the way in,
   rewritten on the way out, so a save is idempotent. A separate prefix rather
   than another `# @layout' field because a probe has no position -- a panel is
   wherever its host is -- and a reader that expected three fields would have
   to know to skip them. */
#define PROBE_PFX   "# @probe"
#define PROBE_TAG   PROBE_PFX " "

string NodeLayout::keyFor (const NodeGraph &graph, int box)
{
    if (box < 0 || box >= (int)graph.boxes().size())
        return "";

    const NodeGraph::Box &b = graph.boxes()[box];

    /* The io node is drawn as two boxes that share a name, so the name alone
       would collide. */
    if (b.isIoSource)
        return b.name + "#in";

    if (b.isIoSink)
        return b.name + "#out";

    return b.name;
}

bool NodeLayout::read (const string &filename, PosMap &out)
{
    out.clear();

    ifstream in(filename.c_str());

    if (!in)
        return false;

    string line;

    while (getline(in, line))
    {
        if (line.compare(0, strlen(LAYOUT_TAG), LAYOUT_TAG) != 0)
            continue;

        istringstream ss(line.substr(strlen(LAYOUT_TAG)));

        string name;
        double x, y;

        /* Anything in the block that is not three fields is the block's own
           prose, and is simply not a position. */
        if (ss >> name >> x >> y)
            out[name] = make_pair(x, y);
    }

    return true;
}

int NodeLayout::apply (const NodeGraph &graph, const PosMap &pos,
                       NodeGraph &target)
{
    int applied = 0;

    for (size_t i = 0; i < graph.boxes().size(); i++)
    {
        PosMap::const_iterator f = pos.find(keyFor(graph, (int)i));

        if (f == pos.end())
            continue;       /* not annotated; keep the computed position */

        target.moveBox((int)i, f->second.first, f->second.second);
        applied++;
    }

    if (applied)
        target.refreshExtent();

    return applied;
}

bool NodeLayout::readProbes (const string &filename, vector<ProbeRef> &out)
{
    out.clear();

    ifstream in(filename.c_str());

    if (!in)
        return false;

    string line;

    while (getline(in, line))
    {
        if (line.compare(0, strlen(PROBE_TAG), PROBE_TAG) != 0)
            continue;

        istringstream ss(line.substr(strlen(PROBE_TAG)));

        ProbeRef p;

        /* All three or nothing. A line with two fields is the block's own
           prose, or a probe someone half-edited by hand; either way there is
           no honest guess at the third. */
        if (ss >> p.node >> p.arg >> p.visual)
            out.push_back(p);
    }

    return true;
}

bool NodeLayout::write (const string &filename, const NodeGraph &graph)
{
    /* Read the whole file, drop the old layout lines, append fresh ones.
     *
     * Note what this does *not* do: it never regenerates a node, an arg or a
     * value from the parsed model. Comments, spacing, `5 ms', and the args
     * buildArgMap() synthesised but nobody wrote are all simply not touched.
     */
    vector<string> lines;

    {
        ifstream in(filename.c_str());

        if (!in)
            return false;

        string line;

        while (getline(in, line))
            if (line.compare(0, strlen(LAYOUT_PFX), LAYOUT_PFX) != 0 &&
                line.compare(0, strlen(PROBE_PFX), PROBE_PFX) != 0)
                lines.push_back(line);
    }

    /* Trailing blank lines would accumulate one per save otherwise. */
    while (!lines.empty() && lines.back().find_first_not_of(" \t\r") == string::npos)
        lines.pop_back();

    /* Into a temporary beside the target, then renamed over it.
     *
     * Truncating the real file and writing into it means a crash, a full disk
     * or a killed process leaves a half-written .dsp -- and that is someone's
     * patch, possibly the only copy. rename() within a directory is atomic, so
     * the file ends up either the old one or the new one, never a prefix of
     * the new one. Beside the target rather than in /tmp, because rename
     * cannot cross a filesystem. */
    const string tmp = filename + ".layout-tmp";

    {
        ofstream out(tmp.c_str());

        if (!out)
            return false;

        for (size_t i = 0; i < lines.size(); i++)
            out << lines[i] << "\n";

        out << "\n" << LAYOUT_PFX << "  Node editor positions. Comments only, so\n"
            << LAYOUT_PFX << "  the file loads exactly as it did without them.\n";

        for (size_t i = 0; i < graph.boxes().size(); i++)
        {
            const NodeGraph::Box &b = graph.boxes()[i];

            /* Not the attached controls. A strip sits directly above the box
               it belongs to; that is computed from the host every time, so
               writing it down would record a position nothing reads and which
               would be wrong the moment the host moved, or the control found a
               second consumer and became a box of its own again. */
            if (b.attachedTo >= 0)
                continue;

            out << LAYOUT_TAG << keyFor(graph, (int)i) << " "
                << (long)(b.x + 0.5) << " " << (long)(b.y + 0.5) << "\n";
        }

        /* The probes, after the positions and in stacking order.
         *
         * Written from the graph rather than from a list passed in, because a
         * panel is a Box and the graph is already where the editor keeps them
         * -- two sources for the same thing is how they come to disagree.
         *
         * No position: a panel is wherever its host ended up, computed from it
         * every time. Writing one down would record something nothing reads
         * and which would be wrong the moment the host moved -- the same
         * reasoning that keeps attached controls out of the block above. */
        for (size_t i = 0; i < graph.boxes().size(); i++)
        {
            const NodeGraph::Box &b = graph.boxes()[i];

            if (!b.isProbe || b.attachedTo < 0)
                continue;

            out << PROBE_TAG << graph.boxes()[b.attachedTo].name << " "
                << b.probeArg << " " << b.probeVisual << "\n";
        }

        out.flush();

        if (!out.good())
        {
            out.close();
            remove(tmp.c_str());
            return false;
        }
    }

    /* thUtil::replaceFile rather than rename(): Windows' rename
       refuses a target that exists, which is every save after the
       first. */
    if (!thUtil::replaceFile(tmp, filename))
    {
        remove(tmp.c_str());
        return false;
    }

    return true;
}
