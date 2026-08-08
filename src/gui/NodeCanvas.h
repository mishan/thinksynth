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
 * View of a NodeGraph: draws it, and lets boxes be dragged and the whole thing
 * zoomed.
 *
 * Still no editing -- wires cannot be made or broken here yet. Hit-testing
 * lives in NodeGraph rather than in this class so it can be tested without a
 * display; this widget only converts coordinates and tracks the drag.
 */
class NodeCanvas : public Gtk::DrawingArea
{
public:
    NodeCanvas (void);

    /* The canvas does not own the graph. */
    void setGraph (NodeGraph *graph);

    double zoom (void) const { return zoom_; }
    void setZoom (double z);

    /* Emitted when a box has been dragged, so a host can mark the document
       dirty and eventually write the position out. */
    typedef sigc::signal<void(int)> type_signal_box_moved;
    type_signal_box_moved signal_box_moved (void) { return m_signal_box_moved_; }

    /* Emitted when the selection changes, with the box index or -1. */
    typedef sigc::signal<void(int)> type_signal_selected;
    type_signal_selected signal_selected (void) { return m_signal_selected_; }

    int selected (void) const { return selBox_; }
    void setSelected (int box);

protected:
    virtual bool on_draw (const Cairo::RefPtr<Cairo::Context> &cr);
    virtual bool on_button_press_event (GdkEventButton *b);
    virtual bool on_button_release_event (GdkEventButton *b);
    virtual bool on_motion_notify_event (GdkEventMotion *m);
    virtual bool on_scroll_event (GdkEventScroll *s);
    virtual bool on_leave_notify_event (GdkEventCrossing *c);

private:
    void drawBox (const Cairo::RefPtr<Cairo::Context> &cr,
                  const NodeGraph::Box &b, bool highlit, bool selected);
    void drawEdge (const Cairo::RefPtr<Cairo::Context> &cr,
                   const NodeGraph::Edge &e);

    /* widget pixels -> graph coordinates */
    void toGraph (double sx, double sy, double &gx, double &gy) const;

    void updateSize (void);

    NodeGraph *graph_;

    double zoom_;

    int dragBox_;             /* box being dragged, or -1        */
    double dragDX_, dragDY_;  /* grab point within that box      */

    int hoverBox_, hoverPort_;
    int selBox_;

    type_signal_box_moved m_signal_box_moved_;
    type_signal_selected m_signal_selected_;
};

#endif /* NODE_CANVAS_H */
