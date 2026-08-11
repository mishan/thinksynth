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

#ifndef NODE_CANVAS_H
#define NODE_CANVAS_H 1

#include "../NodeGraph.h"

/*
 * Read-only view of a NodeGraph.
 *
 * Deliberately draw-only for now: no dragging, no editing, no hit-testing.
 * The point of this stage is to answer the one question the headless tests
 * cannot, which is whether an automatically arranged thinksynth graph is
 * legible at 17 layers deep.
 */
class NodeCanvas : public Gtk::DrawingArea
{
public:
    NodeCanvas (void);

    /* The canvas does not own the graph. */
    void setGraph (NodeGraph *graph);

protected:
    virtual bool on_draw (const Cairo::RefPtr<Cairo::Context> &cr);

private:
    void drawBox (const Cairo::RefPtr<Cairo::Context> &cr,
                  const NodeGraph::Box &b);
    void drawEdge (const Cairo::RefPtr<Cairo::Context> &cr,
                   const NodeGraph::Edge &e);

    NodeGraph *graph_;
};

#endif /* NODE_CANVAS_H */
