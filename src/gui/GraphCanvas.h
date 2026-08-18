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

#ifndef GRAPH_CANVAS_H
#define GRAPH_CANVAS_H 1

#include <gtkmm.h>

/*
 * The part of a canvas that is not about what it draws: a zoom, the
 * conversion between widget pixels and the coordinates a subclass thinks
 * in, and the arithmetic of fitting a drawing to a window.
 *
 * Extracted from NodeCanvas, which had all of it and is now the first
 * user. The second is the composer's canvas, which needs exactly this
 * and nothing else NodeCanvas has -- a patch is a graph with ports and
 * edges and a chain is a sentence, so their *contents* have almost
 * nothing in common. What they share is that both are drawings larger
 * than the window they arrive in.
 *
 * That distinction is why this base is deliberately small. Selection,
 * dragging, wires and hit-testing all look shareable and are not: a
 * NodeGraph box and a chain stage answer "what did I just click" in
 * different vocabularies, and a base that tried to own both would end up
 * with a mode switch in every method. Anything genuinely common can
 * move down later; a base that has to be argued out of is worse than one
 * that has to be argued into.
 *
 * The subclass says how big its drawing is (contentExtent) and this
 * arranges the rest. There is no panning: the canvas sizes itself to the
 * scaled content and lives in a Gtk::ScrolledWindow, so scrolling is the
 * scroller's job and always behaves the way scrolling does everywhere
 * else. Ctrl+wheel zooms; a bare wheel is left alone for that reason.
 *
 * COMPOSITION_HANDOFF.md section 8 argues that .dsp and .gen converge at
 * the grammar only when a language feature pays for it. The same is true
 * of their canvases, and this is the part that has already paid.
 */
class GraphCanvas : public Gtk::DrawingArea
{
public:
    GraphCanvas (void);

    double zoom (void) const { return zoom_; }
    void   setZoom (double z);

    /* Scales so the whole drawing is visible, never magnifying past 1:1.
     *
     * A patch is as wide as its signal chain is deep -- seventeen layers
     * of ts1's kind is about 2900 pixels -- and no amount of layout
     * tuning changes that. Being able to see all of it on opening, and
     * zoom in to work, is the answer to a drawing wider than the screen.
     * Never magnifying because a four-node patch blown up to fill the
     * window looks broken, and the point is only to bring an oversized
     * one down.
     *
     * Deferred if the widget has no size yet: on the first open this is
     * called before GTK has allocated anything, and fitting to a
     * zero-width canvas would give a useless zoom. */
    void zoomToFit (void);

protected:
    /* How big the drawing is, in the subclass's own coordinates, before
       zoom. Zero or negative means "nothing to show", and the base then
       leaves the size and the zoom alone. */
    virtual void contentExtent (double &w, double &h) const = 0;

    /* Widget pixels to the subclass's coordinates. The inverse is a
       multiply and every caller writes it inline, which is why there is
       no toWidget to go with this. */
    void toContent (double sx, double sy, double &cx, double &cy) const;

    /* Call when the drawing's size changed. Re-requests the widget size
       so the scroller knows what it is scrolling. */
    void contentResized (void);

    /* The scroll controller, so a subclass can ask about modifiers on
       its own gestures without making a second one. */
    Glib::RefPtr<Gtk::EventControllerScroll> scroll_;

private:
    void onResize (int width, int height);
    bool onScroll (double dx, double dy);

    double zoom_;

    /* Set by zoomToFit when there was no allocation to fit to; acted on
       by the next resize. */
    bool fitPending_;
};

#endif /* GRAPH_CANVAS_H */
