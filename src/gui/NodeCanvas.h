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

    /* How many boxes a rubber band gathered. Separate from signal_selected,
       which carries the one box the panel shows and is -1 for a group: the
       toolbar still needs to know a group exists so Delete can offer to
       remove all of it. */
    typedef sigc::signal<void(int)> type_signal_selection;
    type_signal_selection signal_selection (void) {
        return m_signal_selection_;
    }

    /* The box the parameter panel is showing: the last one clicked, or the
       only one in the selection. -1 when nothing or when a rubber band
       gathered several, since a panel can only show one node's args. */
    int selected (void) const { return selBox_; }
    void setSelected (int box);

    /* Every selected box, `selected()' among them. Empty or a single entry
       for all the ordinary cases; more after a rubber band. */
    const std::vector<int> &selection (void) const { return sel_; }

    /* True if `box' is in the selection. */
    bool isSelected (int box) const;

    void clearSelection (void);

    /* Draws the body of a probe panel.
     *
     * The canvas knows where a panel is; it does not know what goes in one.
     * The visual modules, their instances and their sample rings all belong to
     * the editor, which is also the thing that arms and disarms them -- so it
     * supplies this, closing over whatever it needs, and the canvas calls it
     * with the panel's box index and a context already translated to the
     * body's origin and clipped to it.
     *
     * Without a painter a panel still draws its frame and its title. That is
     * deliberate: an armed probe whose channel is not playing has nothing to
     * show, and an empty panel says so where a missing one would look like the
     * arming had failed. */
    typedef sigc::slot<void(int, const Cairo::RefPtr<Cairo::Context> &,
                            int, int)> ProbePainter;

    void setProbePainter (const ProbePainter &painter)
    {
        probePainter_ = painter;
    }

    /* How long this canvas has spent rasterising, and how many times.
     *
     * Here because live visualizers mean redrawing on a timer, and whether a
     * full-canvas repaint at 30fps is affordable is a question about *this*
     * drawing code on a real graph -- 1514 boxes and 3094 wires across the
     * corpus, the widest of them 2920 pixels. Measuring a stand-in that draws
     * "something of similar complexity" would answer a different question, and
     * describing the same scene twice is how NODE_EDITOR.md §12's "clicking a
     * wire selects a different wire" happened.
     *
     * scripts/canvasbench is the consumer. Two counters and a clock read per
     * frame is not a cost worth conditionalising away. */
    unsigned long drawCount (void) const { return drawCount_; }
    double drawMicros (void) const { return drawMicros_; }

    void resetDrawStats (void) { drawCount_ = 0; drawMicros_ = 0.0; }

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

    /* A right-click asking what can be done here, with the box and port under
       the pointer (port -1 for none) and the widget coordinates to put a menu
       at.
     *
     * The canvas does not know what the answers are -- probing is the editor's
     * business, since it owns the visual modules and the channel -- so this
     * says where and on what, and nothing about the menu itself. Right-click
     * rather than a modifier because it is the one gesture the canvas does not
     * already spend: left is wire, drag and slider, and the wheel is scroll
     * and zoom. */
    typedef sigc::signal<void(int, int, double, double)> type_signal_context;
    type_signal_context signal_context_requested (void) {
        return m_signal_context_;
    }

    /* A probe panel was double-clicked: the box index.
     *
     * A panel is 128 pixels wide because that is what a node box is, and a
     * spectrogram in 128x80 is a thumbnail. This is the way to a real one, and
     * double-click is the gesture that already means "open this properly"
     * everywhere else. */
    typedef sigc::signal<void(int)> type_signal_activated;
    type_signal_activated signal_probe_activated (void) {
        return m_signal_activated_;
    }

    /* The drawing itself, into any context.
     *
     * Public and separate from the draw callback for two reasons: the timing
     * above wraps one call rather than every return path, and scripts/
     * canvasbench can render a canvas straight into an image surface. That
     * second one matters -- NODE_EDITOR.md §7 says the bugs that matter in
     * this part of the tree are visual, and being able to produce a PNG of a
     * real graph without a screenshot is the difference between looking at one
     * and taking someone's word for it. */
    void drawGraph (const Cairo::RefPtr<Cairo::Context> &cr, int width,
                    int height);

protected:
    /* The draw callback: times drawGraph and keeps the counters. */
    void onDraw (const Cairo::RefPtr<Cairo::Context> &cr, int width,
                 int height);
    void onResize (int width, int height);
    /* Input, through controllers. There are no on_*_event vfuncs in GTK4 and
       no event mask to widen -- a controller receives the kind of thing it is
       for, and is handed the coordinates rather than being asked to fetch
       them. */
    void onPressed (int nPress, double x, double y);
    void onRightPressed (int nPress, double x, double y);
    void onReleased (int nPress, double x, double y);
    void onMotion (double x, double y);
    bool onScroll (double dx, double dy);
    void onLeave (void);

    Glib::RefPtr<Gtk::GestureClick> click_;
    Glib::RefPtr<Gtk::GestureClick> rightClick_;
    Glib::RefPtr<Gtk::EventControllerMotion> motion_;
    Glib::RefPtr<Gtk::EventControllerScroll> scroll_;

private:
    void drawBox (const Cairo::RefPtr<Cairo::Context> &cr, int index,
                  const NodeGraph::Box &b, bool highlit, bool selected);
    void drawEdge (const Cairo::RefPtr<Cairo::Context> &cr, int edge,
                   bool highlit);
    void drawPendingWire (const Cairo::RefPtr<Cairo::Context> &cr);
    void drawSlider (const Cairo::RefPtr<Cairo::Context> &cr,
                     const NodeGraph::Box &b);
    void drawAttached (const Cairo::RefPtr<Cairo::Context> &cr,
                       const NodeGraph::Box &b, bool highlit, bool selected);
    void drawProbe (const Cairo::RefPtr<Cairo::Context> &cr,
                    int index, const NodeGraph::Box &b, bool highlit,
                    bool selected);

    /* widget pixels -> graph coordinates */
    void toGraph (double sx, double sy, double &gx, double &gy) const;

    void updateSize (void);

    NodeGraph *graph_;

    double zoom_;

    int dragBox_;             /* box being dragged, or -1        */
    double dragDX_, dragDY_;  /* grab point within that box      */

    int hoverBox_, hoverPort_;

    /* The selection, and the one box within it the panel speaks for.
     *
     * A vector rather than a set: it is almost always empty or one long, the
     * order is the order things were gathered in, and every use is a scan. */
    std::vector<int> sel_;
    int selBox_;

    /* Rubber band in progress: where the drag started and where the pointer
       is now, both in graph coordinates. bandOn_ rather than a sentinel,
       because a band of zero size at the origin is a real thing to draw. */
    bool bandOn_;
    double bandX0_, bandY0_, bandX1_, bandY1_;

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

    unsigned long drawCount_;
    double drawMicros_;

    ProbePainter probePainter_;

    type_signal_box_moved m_signal_box_moved_;
    type_signal_selected m_signal_selected_;
    type_signal_selection m_signal_selection_;
    type_signal_connect m_signal_connect_;
    type_signal_disconnect m_signal_disconnect_;
    type_signal_refused m_signal_refused_;
    type_signal_control m_signal_control_;
    type_signal_context m_signal_context_;
    type_signal_activated m_signal_activated_;
};

#endif /* NODE_CANVAS_H */
