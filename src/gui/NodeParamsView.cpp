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

#include <gtkmm.h>

#include "NodeGraph.h"
#include "NodeParamsView.h"

NodeParamsView::NodeParamsView (void)
{
    /* Wide enough for a name and a number box beside it. The editor's pane
       can be dragged from here; this is where it starts. */
    set_size_request(240, -1);

    signal_edited().connect(
        sigc::mem_fun(*this, &NodeParamsView::onEdited));
}

void NodeParamsView::setBox (const NodeGraph *graph, int box)
{
    model_.setBox(graph, box);

    thPanel built;

    if (model_.build(built))
    {
        setPanel(built);
        return;
    }

    /* Nothing selected, or a box with no parameters at all. Both are
       states worth saying rather than an empty column. */
    thPanel empty;

    empty.kind = thPanel::NODE_VALUE;
    empty.a = box;
    empty.title = (graph == NULL || box < 0) ? "No node selected"
                                             : "No parameters";
    empty.subtitle = (graph == NULL || box < 0)
        ? "Click a node to see its parameters." : "";

    setPanel(empty);
}

/* What was typed, turned into an intent by the provider and handed on.
 *
 * Nothing is written here, and nothing can be: a node's value lives in the
 * .dsp, and the splice is the window's -- it has the file, the undo and the
 * reload that follows. A refusal is dropped for the same reason the channel
 * panel drops one: every row this panel offers is a number or a name off a
 * list, so the ways to reach one are a stale panel and a graph rewired under
 * it, and in both the right answer is to leave the file alone. */
void NodeParamsView::onEdited (const string &row, const string &valueText)
{
    thPanelEdit edit;

    const thPanelResult result = model_.propose(row, valueText, edit);

    if (!result.ok)
        return;

    /* Reported even when the value is the one the file already holds, which
     * is the one case the channel panel drops.
     *
     * Here that case means something. A node's value is not written when it
     * is typed -- it is kept as a pending edit until Save -- so the panel
     * still shows the file's number while the window holds another. Typing
     * the file's number back is then how a person takes an edit back, and
     * NodeEditor::onParamEdited is what knows that: it erases the pending
     * edit and clears the dirty mark. Dropped here, the window kept the
     * edit, stayed modified, and saved a number the panel was not showing.
     *
     * The guard that matters for a panel catching up is PanelView's, which
     * emits nothing at all while it is pushing a value in. */
    signal_param_edited_.emit(edit.a, edit.row, edit.value);
}
