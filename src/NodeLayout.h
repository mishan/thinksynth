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

#ifndef NODE_LAYOUT_H
#define NODE_LAYOUT_H 1

/*
 * Reading and writing node positions in a .dsp file.
 *
 * Positions live in a structured comment:
 *
 *     # @layout freq 120 40
 *
 * and so do the editor's probes, which are not part of the patch at all:
 *
 *     # @probe filt out meter
 *
 * A comment, so the existing lexer discards it and every .dsp stays loadable
 * by anything that could load it before. An editor that is never used again
 * leaves nothing behind but a few extra comment lines.
 *
 * A probe is a display, not a node: the engine never sees one and a .dsp with
 * probes in it loads identically in a build that has never heard of them. That
 * is the same bargain `# @layout' struck, and it is why these live here rather
 * than anywhere the parser can reach.
 *
 * This is the first, deliberately safest piece of the writer. It only ever
 * rewrites its own `# @layout' and `# @probe' lines and appends a block if
 * none exists;
 * everything else in the file -- comments, formatting, units, the arithmetic
 * the lexer folds away -- is copied through untouched, because it is never
 * regenerated from the parsed model in the first place. NODE_EDITOR.md
 * discusses why regenerating would be a data-loss hazard.
 */

#include <string>
#include <map>
#include <utility>
#include <vector>

using std::string;
using std::map;
using std::pair;
using std::make_pair;
using std::vector;

class NodeGraph;

class NodeLayout {
public:
    typedef map<string, pair<double, double> > PosMap;

    /* One saved probe: which node's output, and what to draw it with. No
       position -- a panel sits on its host, wherever that ended up. */
    struct ProbeRef {
        string node;
        string arg;
        string visual;
    };

    /* Reads `# @layout <node> <x> <y>' lines out of a .dsp. Silently returns
       an empty map if the file has none, which is the normal case. */
    static bool read (const string &filename, PosMap &out);

    /* Applies saved positions to a graph. Boxes with no saved position keep
       the ones layout() computed, so a partially-annotated file still opens
       sensibly. Returns how many boxes were positioned from the file.

       The io node is two boxes sharing one name; they are stored as
       "<name>#in" and "<name>#out". */
    static int apply (const NodeGraph &graph, const PosMap &pos,
                      NodeGraph &target);

    /* Reads `# @probe <node> <arg> <visual>' lines. Order is preserved, since
       it is the order the panels will stack in. */
    static bool readProbes (const string &filename, vector<ProbeRef> &out);

    /* Rewrites the file's layout block.

       The probe panels in `graph' are written out alongside the positions, so
       nothing else has to be passed in: a panel is a Box like any other and
       the graph is where the editor keeps them.

       Every line the block does not own is preserved exactly, and nothing is
       regenerated from the parsed model. Two things are normalised rather than preserved,
       so "byte for byte" would overstate it: line endings come out as \n, and
       trailing blank lines are dropped -- otherwise the blank line before the
       block would accumulate one per save.

       Written to a temporary beside the target and renamed into place, so an
       interrupted save cannot leave a half-written .dsp. */
    static bool write (const string &filename, const NodeGraph &graph);

    /* The key a box is stored under. */
    static string keyFor (const NodeGraph &graph, int box);
};

#endif /* NODE_LAYOUT_H */
