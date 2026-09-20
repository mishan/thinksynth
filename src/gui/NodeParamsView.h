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

#ifndef NODE_PARAMS_VIEW_H
#define NODE_PARAMS_VIEW_H

#include "NodePanel.h"
#include "PanelView.h"

/* One selected node's parameters, on the desktop: NodePanel drawn by
 * PanelView.
 *
 * The binder is thin here because there is nothing to deliver. A node's
 * value is a number in the `.dsp', so what an edit becomes is a splice into
 * that text, and the window owns that decision -- it has the file, the undo
 * and the reload. So this reports the intent and stops, which is what
 * NodeParams did and is why it had a signal rather than a write.
 *
 * This is what src/gui/NodeParams.cpp became, less everything that turned
 * out to be a rule rather than a drawing: which parameters are offered and
 * which only shown, what a wired one says instead of a number, where the
 * range comes from when the file gave none, and the fact that a plugin's
 * named values are a list.
 */
class NodeParamsView : public PanelView
{
public:
    NodeParamsView (void);

    /* Shows a box's parameters. A negative index clears the panel. */
    void setBox (const NodeGraph *graph, int box);

    /* A value committed: box index, param name, new value. The shape
       NodeEditor was already written against. */
    typedef sigc::signal<void(int, string, double)> type_signal_param_edited;

    type_signal_param_edited signal_param_edited (void)
    {
        return signal_param_edited_;
    }

private:
    void onEdited (const string &row, const string &valueText);

    NodePanel model_;
    type_signal_param_edited signal_param_edited_;
};

#endif /* NODE_PARAMS_VIEW_H */
