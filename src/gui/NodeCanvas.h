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

    /* Scales so the whole graph is visible, never magnifying past 1:1.
    
       A patch is as wide as its signal chain is deep -- 17 layers of ts1's
       kind is about 2900 pixels -- and no amount of layout tuning changes
       that. Being able to see all of it on opening, and zoom in to work, is
       the answer to a graph wider than the screen.
    
       Deferred if the widget has no size yet: on the first open it is called
       before GTK has allocated anything, and fitting to a zero-width canvas
       would give a useless zoom. */
    void zoomToFit (void);

    /* Emitted when a box has been dragged, so a host can mark the document
       dirty and eventually write the position out. */
    typedef sigc::signal<void(int)> type_signal_box_moved;
    type_signal_box_moved signal_box_moved (void) { return m_signal_box_moved_; }

    /* Emitted when the selection changes, with the box index or -1. */
    typedef sigc::signal<void(int)> type_signal_selected;
    type_signal_selected signal_selected (void) { return m_signal_selected_; }

    int selected (void) const { return selBox_; }
    void setSelected (int box);

    /* Emitted when a wire is dragged between two ports, and when one is
       asked to be removed. The canvas does not change the graph itself --
       the window owns that, because it also has to record the edit. */
    typedef sigc::signal<void(int, int, int, int)> type_signal_connect;
    type_signal_connect signal_connect_requested (void) {
        return m_signal_connect_;
    }

    typedef sigc::signal<void(int)> type_signal_disconnect;
    type_signal_disconnect signal_disconnect_requested (void) {
        return m_signal_disconnect_;
    }

    /* Emitted with a reason when a drop is refused, so the window can say
       why rather than the wire just vanishing. */
    typedef sigc::signal<void(string)> type_signal_refused;
    type_signal_refused signal_refused (void) { return m_signal_refused_; }

    /* Emitted while a control's slider is dragged, and once more when it is
       released -- the window shows the value live but only records the edit
       on release, so a drag across the track is one change and not fifty. */
    typedef sigc::signal<void(int, double, bool)> type_signal_control;
    type_signal_control signal_control_changed (void) {
        return m_signal_control_;
    }

protected:
    virtual bool on_draw (const Cairo::RefPtr<Cairo::Context> &cr);
    virtual void on_size_allocate (Gtk::Allocation &alloc);
    virtual bool on_button_press_event (GdkEventButton *b);
    virtual bool on_button_release_event (GdkEventButton *b);
    virtual bool on_motion_notify_event (GdkEventMotion *m);
    virtual bool on_scroll_event (GdkEventScroll *s);
    virtual bool on_leave_notify_event (GdkEventCrossing *c);

private:
    void drawBox (const Cairo::RefPtr<Cairo::Context> &cr,
                  const NodeGraph::Box &b, bool highlit, bool selected);
    void drawEdge (const Cairo::RefPtr<Cairo::Context> &cr, int edge,
                   bool highlit);
    void drawPendingWire (const Cairo::RefPtr<Cairo::Context> &cr);
    void drawSlider (const Cairo::RefPtr<Cairo::Context> &cr,
                     const NodeGraph::Box &b);
    void drawAttached (const Cairo::RefPtr<Cairo::Context> &cr,
                       const NodeGraph::Box &b, bool highlit, bool selected);

    /* widget pixels -> graph coordinates */
    void toGraph (double sx, double sy, double &gx, double &gy) const;

    void updateSize (void);

    NodeGraph *graph_;

    double zoom_;

    int dragBox_;             /* box being dragged, or -1        */
    double dragDX_, dragDY_;  /* grab point within that box      */

    int hoverBox_, hoverPort_;
    int selBox_;

    /* Wire being dragged: the port it started at, and where the loose end
       currently is, in graph coordinates. */
    int wireBox_, wirePort_;
    double wireX_, wireY_;
    int wireTargetBox_, wireTargetPort_;
    bool wireTargetOk_;

    int hoverEdge_;

    /* Control whose slider is being dragged, or -1. */
    int dragSlider_;

    /* Set by zoomToFit when there was no allocation to fit to; acted on by
       the next size-allocate. */
    bool fitPending_;

    type_signal_box_moved m_signal_box_moved_;
    type_signal_selected m_signal_selected_;
    type_signal_connect m_signal_connect_;
    type_signal_disconnect m_signal_disconnect_;
    type_signal_refused m_signal_refused_;
    type_signal_control m_signal_control_;
};

#endif /* NODE_CANVAS_H */
