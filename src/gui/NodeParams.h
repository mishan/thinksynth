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

#ifndef NODE_PARAMS_H
#define NODE_PARAMS_H 1

#include "../NodeGraph.h"

/*
 * The parameters of one selected node.
 *
 * ArgTable does the same job for a channel's args, but against live thArg
 * pointers that it edits in place. This one works from the graph's snapshot
 * and reports edits by signal, because what a node's arg needs changing is the
 * .dsp text -- there is no live thArg to poke. The window owns that decision;
 * this widget only knows what was typed.
 */
class NodeParams : public Gtk::VBox
{
public:
    NodeParams (void);

    /* Shows a box's parameters. A negative index clears the panel. */
    void setBox (const NodeGraph *graph, int box);

    /* Emitted when a value is committed: box index, param name, new value. */
    typedef sigc::signal<void(int, string, double)> type_signal_param_edited;
    type_signal_param_edited signal_param_edited (void) {
        return m_signal_param_edited_;
    }

protected:
    void addRow (Gtk::Grid *grid, int row, const NodeGraph::Param &p);
    void onSpinActivate (Gtk::SpinButton *spin, string name);

private:
    const NodeGraph *graph_;
    int box_;

    Gtk::Label title_;
    Gtk::Label subtitle_;
    Gtk::ScrolledWindow scroller_;
    Gtk::Grid *grid_;

    /* Set while setBox() populates, so the spin buttons it creates do not
       report their initial values back as edits. */
    bool loading_;

    type_signal_param_edited m_signal_param_edited_;
};

#endif /* NODE_PARAMS_H */
