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

#ifndef COMPOSER_CANVAS_H
#define COMPOSER_CANVAS_H

#include <string>
#include <vector>
#include <functional>

#include <gtkmm.h>

#include "libthink/thcomposer.h"   /* thcInputEvent */
#include "GraphCanvas.h"
#include "thcGenEdit.h"

class thcScheduler;
struct thcStage;

/* The composer's node view: one row per chain, reading left to right
 * the way a chain executes -- name, stages, sinks -- with ghost boxes
 * where something can be added. Events flow along the arrows; that is
 * the whole topology, which is why this is its own small canvas rather
 * than NodeCanvas taught new tricks: a .dsp is a graph with ports and
 * edges, a chain is a sentence.
 *
 * A stage that exports composer_draw gets it drawn INSIDE its box --
 * the algorithm's face on the node that runs it (the euclid ring, the
 * L-system's contour, evolve's fitness curve). Sink boxes wear their
 * channel's color, the same golden-angle hue the piano roll gives that
 * channel's notes, so "which row makes which notes" is answered by
 * looking.
 *
 * A stage box carries its params. Collapsed it shows the plugin's face
 * (composer_draw) and nothing else; the triangle in its title bar opens
 * it into a column of inline sliders, one per numeric param, laid out on
 * the box itself. Expandable rather than always open because `gen::life'
 * has ten params and a chain of three such stages would be a wall --
 * and because the face is what most stages are worth looking at while
 * they play. Which stages are open is a view preference, kept here and
 * keyed by name so it survives a reload and a reorder; it is not written
 * to the file, because where you were looking is not part of the piece.
 *
 * A stage whose module also exports composer_input has a picture that is
 * a *control*: clicks on it are handed straight to the plugin, in the
 * coordinates it drew in. Double-clicking such a stage enlarges its draw
 * to fill the canvas, because a Life board in a hundred-pixel box is six
 * pixels a cell and nobody can click that. Escape, or another
 * double-click, puts it back.
 *
 * The canvas does not edit anything itself. It reports -- a selection,
 * a drag that wants stages reordered -- and ComposerWindow performs the
 * edit through thcGenEdit and reloads. One writer, as everywhere else.
 * Input is not an exception to that: a click goes to the plugin's own
 * state and changes what is playing, which is a performance, not a file
 * edit. Capturing it into the file is a separate, deliberate act.
 */
class ComposerCanvas : public GraphCanvas
{
public:
    ComposerCanvas (void);

    /* What to show. `doc' is the parsed piece (authored spellings and
       all); `sched' is the live piece, for mute state and for the
       instance state composer_draw reads. Both stay owned by the
       caller; SetPiece is called again after every reload. */
    void SetPiece (const thcGenEdit::Doc *doc, thcScheduler *sched);

    struct Selection
    {
        enum Kind { NONE, CHAIN, STAGE, SINK, ADD_STAGE, ADD_SINK,
                    ADD_CHAIN, KNOB } kind;

        size_t chain;        /* meaningful for all but NONE/ADD_CHAIN   */
        size_t index;        /* stage or sink index                     */

        Selection (void) : kind(NONE), chain(0), index(0) {}

        bool operator== (const Selection &o) const
        {
            return kind == o.kind && chain == o.chain && index == o.index;
        }
    };

    const Selection &selection (void) const { return sel_; }
    void select (const Selection &sel);

    /* Selection changed by a click (or by select()). */
    sigc::signal<void (const Selection &)> sigSelection;

    /* A drag dropped stage `from' at position `to' in chain `chain'. */
    sigc::signal<void (size_t, int, int)> sigMoveStage;

    /* Someone asked a stage for its params: which stage, and where its
       box is in widget pixels for the popover to point at.
     *
       A popover rather than the box growing in place. Growing was the
       first try and it does not survive contact with a real piece: a
       stage with ten params is taller than its chain's row, so opening
       one shoves the chains below it down, and opening two makes the
       canvas unreadable -- and the rows are drawn at canvas scale, which
       is small on purpose. A popover is real widgets at the window's own
       font, with the panel's spin buttons, unit menus and knob bindings
       already working in it, and it costs the drawing nothing. */
    sigc::signal<void (size_t, size_t, Gdk::Rectangle)> sigParams;

    /* A knob node dragged: its name, the new value, and whether this is
       the committing one. Same shape and same reason as sigParams'
       neighbours -- live all the way down the drag, spliced once at the
       end. */
    sigc::signal<void (std::string, double, bool)> sigKnob;

    /* A wire dropped from a knob node onto a stage: the knob's name,
       which stage, and where its box is for the popover that asks which
       param the wire is for.
     *
       The canvas cannot answer that question itself -- it would need a
       list of the stage's params with their types, which is the Edit
       panel's business -- and it should not: dropping a wire on a box
       with six params is genuinely ambiguous, and guessing would be
       worse than asking. */
    sigc::signal<void (std::string, size_t, size_t,
                       Gdk::Rectangle)> sigBindKnob;

    /* Which stage is filling the canvas, or NONE. Public so the window
       can label what it is showing and offer to capture it. */
    const Selection &enlarged (void) const { return enlarged_; }
    void setEnlarged (const Selection &sel);

    /* The enlarged stage changed (or went away). */
    sigc::signal<void (const Selection &)> sigEnlarged;

    /* Where a stage's box is, in widget pixels. */
    bool stageRect (size_t chain, size_t stage, Gdk::Rectangle &at) const;

    /* Where a knob node's value track is, in widget pixels, and where
       its output port is. False if there is no such knob. */
    bool knobTrack (const std::string &name,
                    double &x0, double &x1, double &y) const;
    bool knobPort (const std::string &name, double &x, double &y) const;

    /* Where its params handle is, in widget pixels.
     *
       Public because the only other way to find out is to repeat the
       layout arithmetic, and a caller that repeated it would be testing
       its own copy of it. */
    bool paramsHandle (size_t chain, size_t stage,
                       double &x, double &y) const;

    /* A gesture, in widget pixels, without a mouse.
     *
       The controllers call these and so can a harness, which is the
       point: every handler converts widget pixels to laid-out
       coordinates on the way in, and a missed conversion is a click that
       lands somewhere else -- silently, and only at a zoom nobody tests
       at. Making the entry public costs nothing and turns "I checked the
       conversions by reading them" into something ctest can say. */
    void pressAt (double sx, double sy, int button, int nPress);
    void motionTo (double sx, double sy);
    void releaseAt (double sx, double sy, int button);

protected:
    void onDraw (const Cairo::RefPtr<Cairo::Context> &cr, int width,
                 int height);
    void onPressed (int nPress, double x, double y, int button);
    void onDragBegin (double x, double y);
    void onDragUpdate (double dx, double dy);
    void onDragEnd (double dx, double dy);
    void onReleased (int nPress, double x, double y, int button);
    void onMotion (double x, double y);
    bool onKey (guint keyval, guint keycode, Gdk::ModifierType state);

private:
    /* One clickable box, laid out by rebuild(). */
    struct Box
    {
        Selection what;
        double x, y, w, h;
        std::string title;       /* e.g. "gen::lsystem"                 */
        std::string sub;         /* stage name, or the sink's target    */
        thcStage *live;          /* for composer_draw; may be NULL      */
        int channel;             /* sinks: for the hue                  */

        /* Knob nodes: what the knob reads and the range it reads it in.
           Unused, and left alone, by every other kind. */
        double kv, klo, khi;
        bool ghost;              /* an add-slot                         */
    };

    void rebuild (void);
    const Box *hit (double x, double y) const;
    const Box *boxFor (size_t chain, size_t stage) const;

    /* How wide and tall the laid-out rows are, for the base's zoom. */
    void contentExtent (double &w, double &h) const;

    /* Where the params handle is, in box space. */
    static void twistyRect (const Box &b, double &x, double &y, double &s);

    Gdk::Rectangle boxRect (const Box &b) const;

    /* A knob node's value track and its output port, in box space. */
    static void knobTrackRect (const Box &b, double &x0, double &x1,
                               double &y);
    static void knobPortAt (const Box &b, double &x, double &y);

    const Box *knobBox (const std::string &name) const;

    /* Every wire the piece declares: from a knob node's port to the
       stage box that reads it. Laid out on demand rather than stored,
       because a binding lives in the live stage and the live stage is
       replaced on every reload. */
    void eachWire (const std::function<void (const Box &knob,
                                             const Box &stage,
                                             const std::string &param)>
                   &fn) const;

    void drawKnob (const Cairo::RefPtr<Cairo::Context> &cr,
                   const Box &box, bool selected) const;
    void drawWires (const Cairo::RefPtr<Cairo::Context> &cr) const;

    /* The live stage filling the canvas, or NULL. */
    thcStage *enlargedStage (void) const;

    /* Where the enlarged draw sits, in widget coordinates. */
    void enlargedRect (double &x, double &y, double &w, double &h) const;

    /* Hand a gesture to the enlarged plugin, if there is one and the
       point is inside its picture. True if it was taken. */
    bool feedInput (thcInputType type, double x, double y, int button);
    void drawBox (const Cairo::RefPtr<Cairo::Context> &cr,
                  const Box &box, bool selected) const;

    const thcGenEdit::Doc *doc_;
    thcScheduler *sched_;

    std::vector<Box> boxes_;
    std::vector<double> rowY_;   /* top of each chain row               */

    Selection sel_;

    /* NONE unless a stage is filling the canvas. Kept as a Selection
       rather than a Box index because rebuild() renumbers the boxes and
       an enlarged view has to survive a reload. */
    Selection enlarged_;

    /* True between a press the plugin took and its release, so a drag
       paints rather than selecting or moving anything, and which button
       started it -- a drag has no button of its own, so the one the
       press carried is the one the whole gesture is made with. */
    bool feeding_;
    int  feedButton_;

    /* The knob node whose value is being dragged, or -1. */
    int dragKnob_;

    /* The knob a wire is being pulled from, or -1, and where the far end
       of it is right now, in laid-out coordinates. */
    int  wireFrom_;
    double wireX_, wireY_;

    /* Drag state: which stage box is in flight, and where the pointer
       has carried it. dropAt_ is the insertion index the drop would
       land on, -1 while the drag has not left its own slot. */
    int    dragBox_;
    double dragDx_;
    int    dropAt_;
    double pressX_, pressY_;
};

#endif /* COMPOSER_CANVAS_H */
