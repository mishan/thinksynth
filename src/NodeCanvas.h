/*
 * Copyright (C) 2004-2026 The thinksynth authors
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

#include <set>

#include <sigc++/sigc++.h>

#include "NodeGraph.h"
#include "CanvasContent.h"

/*
 * View of a NodeGraph: draws it, and lets boxes be dragged and the whole thing
 * zoomed.
 *
 * Hit-testing lives in NodeGraph rather than in this class so it can be
 * tested without a display; this class converts coordinates, tracks the
 * drag, and draws. It is the content of a canvas and not a widget -- see
 * CanvasContent.h -- so it compiles without a toolkit, and the desktop
 * wraps it in NodeCanvasWidget (src/gui/) for the controllers and the
 * scroller. Everything a shell hands in arrives in shell pixels through
 * the on*() handlers below.
 */
class NodeCanvas : public CanvasContent
{
public:
    NodeCanvas (void);

    /* The canvas does not own the graph. */
    void setGraph (NodeGraph *graph);
    const NodeGraph *graph (void) const { return graph_; }

    /* What a control's strip at `x', `y' in the shell's pixels has no room
       for: its whole label, and its value as the parameter panel spells
       it, with the range. Empty anywhere else. */
    string tipAt (double x, double y) const;

    /* zoom(), setZoom() and zoomToFit() come from CanvasContent, which is
       where the view transform went when the composer's canvas turned out
       to want exactly it and nothing else here. */

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

    /* `boxes' held, as a band leaves them, with `shown' -- one of them, or
       -1 for none unless there is only one -- the box whose parameters show. */
    void setSelection (const std::vector<int> &boxes, int shown);

    /* What is selected, by name, and which of it the panel shows: a parse
       renumbers the boxes, and a name survives it. By NodeLayout::keyFor,
       which tells the io node's two halves apart, and not the boxes on
       another -- a press on one selects its host -- since a control a wire
       has just put on a node keeps its name. */
    struct Held {
        std::vector<string> keys;
        string shown;
    };

    Held held (void) const;

    /* Selects again what is still there of `h'. */
    void hold (const Held &h);

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
     * describing the same scene twice is how docs/NODE_EDITOR.md's "clicking a
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

    /* A wire clicked, by its index, or -1 when it is let go of. Delete then
       asks for it to be cut, through signal_disconnect_requested. */
    type_signal_disconnect signal_wire_selected (void) {
        return m_signal_wire_selected_;
    }

    int selectedWire (void) const { return selEdge_; }

    /* A right-click on a wire: its index and where to put a menu. */
    typedef sigc::signal<void(int, double, double)> type_signal_wire_context;
    type_signal_wire_context signal_wire_context_requested (void) {
        return m_signal_wire_context_;
    }

    /* Delete pressed with boxes selected: the editor decides which of them
       can go, as it does for its own Delete. */
    typedef sigc::signal<void()> type_signal_delete;
    type_signal_delete signal_delete_requested (void) {
        return m_signal_delete_;
    }

    /* The selection moved by a key, with a box in it. Not signal_box_moved:
       a run of presses is one edit, where every drag is one of its own. */
    type_signal_box_moved signal_nudged (void) { return m_signal_nudged_; }

    /* The drawing itself, into any context.
     *
     * Public and separate from the draw callback for two reasons: the timing
     * above wraps one call rather than every return path, and scripts/
     * canvasbench can render a canvas straight into an image surface. That
     * second one matters -- docs/NODE_EDITOR.md says the bugs that matter in
     * this part of the tree are visual, and being able to produce a PNG of a
     * real graph without a screenshot is the difference between looking at one
     * and taking someone's word for it. */
    void drawGraph (const Cairo::RefPtr<Cairo::Context> &cr, int width,
                    int height);

    /* The shell's draw: times drawGraph and keeps the counters. */
    void draw (const Cairo::RefPtr<Cairo::Context> &cr, int width,
               int height) override;

    /* Input, in shell pixels, from whatever the shell has -- gesture
       controllers on the desktop, pointer events in a browser, a harness
       with neither. Every one converts at the door. */
    void onPressed (int nPress, double x, double y, unsigned mods = 0);
    void onRightPressed (int nPress, double x, double y);
    void onReleased (int nPress, double x, double y);
    void onMotion (double x, double y);
    void onLeave (void);

    /* Arrows go to the nearest box that way; with Shift they move the
       selection, 1 pixel with Ctrl too. Delete asks for the selection to
       go, Escape lets go of what is held, and Select All takes every box
       not on another. Ctrl+arrow is left to the shell's scroller. */
    bool keyPressed (Key key, unsigned mods = 0) override;

    /* A press, a drag, a wire or a band under way: keys wait for it. */
    bool busy (void) const;

    /* Nodes drawn with a title of their own color: what growth or a search
       added to the graph the piece declares. */
    void setMarked (const std::set<string> &names) { marked_ = names; }

protected:
    /* How big the graph is, for the base's zoom-to-fit and sizing. */
    void contentExtent (double &w, double &h) const override;

private:
    void drawBox (const Cairo::RefPtr<Cairo::Context> &cr, int index,
                  const NodeGraph::Box &b, bool highlit, bool selected);
    void drawEdge (const Cairo::RefPtr<Cairo::Context> &cr, int edge,
                   bool highlit, bool selected);

    /* Lets go of a selected wire, saying so. */
    void dropWire (void);
    void drawPendingWire (const Cairo::RefPtr<Cairo::Context> &cr);
    void drawSlider (const Cairo::RefPtr<Cairo::Context> &cr,
                     const NodeGraph::Box &b);
    void drawAttached (const Cairo::RefPtr<Cairo::Context> &cr,
                       const NodeGraph::Box &b, bool highlit, bool selected);
    void drawProbe (const Cairo::RefPtr<Cairo::Context> &cr,
                    int index, const NodeGraph::Box &b, bool highlit,
                    bool selected);

    NodeGraph *graph_;

    int dragBox_;             /* box being dragged, or -1        */
    double dragDX_, dragDY_;  /* grab point within that box      */
    double dragX0_, dragY0_;  /* where that box was on the press */

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

    /* A pan under way: where the pointer was on the screen, and the scroll
       then, both in shell pixels. The screen and not the canvas, whose
       pixels slide under a pointer held still while it scrolls. */
    bool panOn_ = false;
    double panSX_ = 0, panSY_ = 0, panX0_ = 0, panY0_ = 0;

    /* A band drawn with Shift adds to what was selected when it began. */
    std::vector<int> bandBase_;

    /* The free box nearest `from' going (dx, dy), or -1. */
    int nearest (int from, double dx, double dy) const;

    /* Wire being dragged: the port it started at, and where the loose end
       currently is, in graph coordinates. */
    int wireBox_, wirePort_;
    double wireX_, wireY_;
    int wireTargetBox_, wireTargetPort_;
    bool wireTargetOk_;

    int hoverEdge_;

    /* Control whose slider is being dragged, or -1. */
    int dragSlider_;

    unsigned long drawCount_;
    double drawMicros_;

    ProbePainter probePainter_;

    type_signal_box_moved m_signal_box_moved_;
    std::set<string> marked_;
    type_signal_selected m_signal_selected_;
    type_signal_selection m_signal_selection_;
    type_signal_connect m_signal_connect_;
    type_signal_disconnect m_signal_disconnect_;
    type_signal_refused m_signal_refused_;
    type_signal_control m_signal_control_;
    type_signal_context m_signal_context_;
    type_signal_activated m_signal_activated_;
    type_signal_delete m_signal_delete_;
    type_signal_disconnect m_signal_wire_selected_;
    type_signal_wire_context m_signal_wire_context_;

    int selEdge_;               /* the wire clicked, or -1 */
    type_signal_box_moved m_signal_nudged_;
};

#endif /* NODE_CANVAS_H */
