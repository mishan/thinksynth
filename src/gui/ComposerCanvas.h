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
 * A stage box carries its params behind a handle -- the three little
 * sliders in its title bar -- which selects the stage and asks the
 * window for a popover beside the box. The box itself never changes
 * size, and that is the whole of why it is a popover: the first version
 * of this expanded the box in place with a column of inline tracks, and
 * a stage with eight params is taller than its chain's row, so opening
 * one shoved every chain below it down and opening two made the canvas
 * unreadable. Nothing here is typed into either, at eight pixels a row.
 * The canvas asks; ComposerWindow answers with the Edit panel's own
 * rows, which already know about units and knob bindings.
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
                    ADD_CHAIN } kind;

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

    /* Which stage is filling the canvas, or NONE. Public so the window
       can label what it is showing and offer to capture it. */
    const Selection &enlarged (void) const { return enlarged_; }
    void setEnlarged (const Selection &sel);

    /* The enlarged stage changed (or went away). */
    sigc::signal<void (const Selection &)> sigEnlarged;

    /* Where the enlarged picture would go, in laid-out coordinates.
       False if no stage is enlarged. Answers about the view rather than
       about whether the stage has a picture to put there.
     *
       Public so that "it follows the viewport" is a thing a harness can
       ask rather than a thing the code claims. */
    bool enlargedArea (double &x, double &y, double &w, double &h) const;

    /* Where a stage's box is, in widget pixels. */
    bool stageRect (size_t chain, size_t stage, Gdk::Rectangle &at) const;

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

    /* Drag state: which stage box is in flight, and where the pointer
       has carried it. dropAt_ is the insertion index the drop would
       land on, -1 while the drag has not left its own slot. */
    int    dragBox_;
    double dragDx_;
    int    dropAt_;
    double pressX_, pressY_;
};

#endif /* COMPOSER_CANVAS_H */
