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
            if (line.compare(0, strlen(LAYOUT_PFX), LAYOUT_PFX) != 0)
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

            out << LAYOUT_TAG << keyFor(graph, (int)i) << " "
                << (long)(b.x + 0.5) << " " << (long)(b.y + 0.5) << "\n";
        }

        out.flush();

        if (!out.good())
        {
            out.close();
            remove(tmp.c_str());
            return false;
        }
    }

    if (rename(tmp.c_str(), filename.c_str()) != 0)
    {
        remove(tmp.c_str());
        return false;
    }

    return true;
}
