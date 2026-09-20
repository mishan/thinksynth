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

#ifndef NODE_PANEL_H
#define NODE_PANEL_H 1

/*
 * One selected node's parameters, as a thPanel.
 *
 * The third provider (see PanelModel.h), and the one whose edits reach
 * nothing live. A channel's arg and a piece's knob are objects a shell can
 * write; a node's value is a number in a `.dsp', and what changes it is a
 * splice into that text -- thcGenEdit's shape of edit, NodeEdit::setValue on
 * the desktop and tw_edit_set_value in the browser. So there is a propose()
 * here and no deliver(): the intent says which node, which arg and what
 * number, and the caller writes the file.
 *
 * Built from NodeGraph's snapshot rather than from a live tree, which is
 * what lets the same panel be described for a patch nobody has loaded -- the
 * browser's node editor works over the text in a document and never puts it
 * on a channel.
 *
 * What the four kinds of parameter become is the whole of the work:
 *
 *   a plain number        offered, as a number box
 *   named values          offered, as a list -- the names the plugin gave
 *   an output             shown, not offered: the plugin writes it every
 *                         window, and a box to type in would invite an edit
 *                         the next one overwrites
 *   a wire, a control,    shown, with where the value comes from, because
 *   a note, a name        the thing to change is the wire and not the field
 */

#include <string>

#include "PanelModel.h"

class NodeGraph;

class NodePanel
{
public:
    NodePanel (void);

    /* Which box of which graph. A negative box, or no graph, is an empty
     * panel.
     *
     * The graph is borrowed and not owned, and a panel is exactly as stale
     * as it is: rebuilding the graph from changed text makes new boxes, and
     * the caller re-opens the panel from the same rebuild. */
    void setBox (const NodeGraph *graph, int box);

    /* The panel. Its title is the node's name and its subtitle the plugin's
       spelling, because a panel over one node of thirty has to say which.
       False for a box with no parameters at all. */
    bool build (thPanel &out) const;

    /* What an edit of `row' to `valueText' means: a NODE_VALUE intent whose
       `a' is the box. Refuses a row that is shown rather than offered, which
       is how a stale panel over a rewired graph fails.
     *
     * There is no deliver(). See the header. */
    thPanelResult propose (const string &row, const string &valueText,
                           thPanelEdit &out) const;

    /* True if any of this box's parameters is a plain number somebody could
     * type into.
     *
     * Asked here rather than worked out again by whoever wants to know --
     * the page's node view, which decides whether a box is worth opening a
     * panel for, and the harnesses, which decide whether it is worth
     * clicking on. Three answers to "is there anything to set here" is two
     * too many, and the one that matters is the one the panel gives. */
    static bool settable (const NodeGraph *graph, int box);

private:
    const NodeGraph *graph_;
    int box_;
};

#endif /* NODE_PANEL_H */
