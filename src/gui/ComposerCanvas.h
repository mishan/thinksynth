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
 * The canvas does not edit anything itself. It reports -- a selection,
 * a drag that wants stages reordered -- and ComposerWindow performs the
 * edit through thcGenEdit and reloads. One writer, as everywhere else.
 */
class ComposerCanvas : public Gtk::DrawingArea
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

protected:
    void onDraw (const Cairo::RefPtr<Cairo::Context> &cr, int width,
                 int height);
    void onPressed (int nPress, double x, double y);
    void onDragBegin (double x, double y);
    void onDragUpdate (double dx, double dy);
    void onDragEnd (double dx, double dy);

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
    void drawBox (const Cairo::RefPtr<Cairo::Context> &cr,
                  const Box &box, bool selected) const;

    const thcGenEdit::Doc *doc_;
    thcScheduler *sched_;

    std::vector<Box> boxes_;
    std::vector<double> rowY_;   /* top of each chain row               */

    Selection sel_;

    /* Drag state: which stage box is in flight, and where the pointer
       has carried it. dropAt_ is the insertion index the drop would
       land on, -1 while the drag has not left its own slot. */
    int    dragBox_;
    double dragDx_;
    int    dropAt_;
    double pressX_, pressY_;
};

#endif /* COMPOSER_CANVAS_H */
