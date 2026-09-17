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

#ifndef CANVAS_CONTENT_H
#define CANVAS_CONTENT_H 1

#include <cairomm/context.h>

/*
 * The content of a canvas: what it draws and what it does when clicked,
 * with no widget around it.
 *
 * NodeCanvas and ComposerCanvas used to be gtkmm widgets. Everything
 * that made them what they are -- the drawing, the hit-testing, the
 * selection, the drags, the enlarged view -- was already written over
 * cairomm and plain doubles; what tied them to the toolkit was a
 * handful of calls the other way: ask for a redraw, say the drawing
 * changed size, ask how much of it can be seen, take the keyboard
 * focus. This class is those calls, as virtuals a *shell* implements,
 * and the zoom, which was the one piece of view state the toolkit base
 * (GraphCanvas) used to hold.
 *
 * Two shells exist. On the desktop it is a Gtk::DrawingArea in a
 * scrolled window (src/gui/GraphCanvas.h), which creates the gesture
 * controllers and forwards them to the handlers below. In the browser
 * it is a <canvas> element with pointer events, over the same class
 * compiled to wasm, drawing through the cairo stand-in (JAM_M6.md,
 * sections 3 and 6). One drawing and one behaviour, two shells: a box
 * is drawn and a click is decided by the one piece of code that
 * exists to do it, on every platform, and the shell is thin enough to
 * be written twice.
 *
 * The guard on that promise is the build: the content classes compile
 * with no toolkit on the include path (src/CMakeLists.txt,
 * think_nodecanvas and think_composercanvas), so the first Gtk:: that
 * gets into one is a build failure and not a slow drift back.
 *
 * Coordinates. A subclass thinks in its own laid-out units; a shell
 * hands it pixels. toContent() divides by the zoom, and every handler
 * converts once at the door. Widths and heights that leave this class
 * for a shell -- resizeShell, the rectangles the composer canvas emits
 * for a popover -- are pixels, already multiplied.
 */

/* A rectangle in shell pixels, for the places a canvas has to say
   where something is to whatever is around it -- a popover to point, a
   harness to press. A struct of its own rather than Gdk::Rectangle, so
   the class saying it needs no toolkit to say it in. */
struct CanvasRect
{
    int x, y, w, h;

    CanvasRect (void) : x(0), y(0), w(0), h(0) {}
    CanvasRect (int x_, int y_, int w_, int h_)
        : x(x_), y(y_), w(w_), h(h_) {}
};

class CanvasContent
{
public:
    CanvasContent (void);
    virtual ~CanvasContent (void);

    /* The keys a canvas answers to, named here so a shell maps its
       toolkit's keysym to one of these and the content never sees a
       GDK_KEY_ or a KeyboardEvent.key. Only what is used; a new one is
       a line here and a line in each shell. */
    enum Key
    {
        KEY_NONE = 0,
        KEY_ESCAPE
    };

    /* ---- what the shell calls ---- */

    /* Draws everything into `cr', which is `width' by `height' pixels
       with the origin at the top left. The zoom is the content's to
       apply. Public, and separate from any toolkit draw callback, so a
       harness can render a canvas into an image surface. */
    virtual void draw (const Cairo::RefPtr<Cairo::Context> &cr,
                       int width, int height) = 0;

    /* A key went down; true if the content took it. */
    virtual bool keyPressed (Key key) { (void)key; return false; }

    /* The shell's size changed. Takes the fit zoomToFit() had to put
       off because there was nothing to fit to yet. */
    void shellResized (void);

    /* ---- the view ---- */

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
     * Deferred if the shell has no size yet: on the first open this is
     * called before anything has been laid out, and fitting to a
     * zero-width view would give a useless zoom. shellResized() is
     * where the deferred fit happens. */
    void zoomToFit (void);

    /* Shell pixels to the content's own coordinates. The inverse is a
       multiply and every caller writes it inline, which is why there is
       no toShell to go with this. */
    void toContent (double sx, double sy, double &cx, double &cy) const;

    /* What can be seen right now, in the content's coordinates: where
       the view starts and how big it is. Not the drawing's size -- the
       drawing is the whole thing, scrolled off or not. Anything that
       wants to fill the *view*, the composer's enlarged stage say, has
       to ask this, or it will lay itself out across the content and be
       somewhere else the moment anybody scrolls.
     *
     * Falls back to the whole drawing when the shell cannot say, which
     * is what a canvas built by a harness with no shell looks like. */
    void visibleRect (double &x, double &y, double &w, double &h) const;

    /* Call when the drawing's size changed: tells the shell how big it
       is now, in pixels, so a scroller knows what it is scrolling. */
    void contentResized (void);

protected:
    /* How big the drawing is, in the content's own coordinates, before
       zoom. Zero or negative means "nothing to show", and the size and
       the zoom are then left alone. */
    virtual void contentExtent (double &w, double &h) const = 0;

    /* ---- what the content asks of its shell ----
     *
     * All with a default that does nothing, so a content class stands
     * on its own in a harness with no shell at all. */

    /* Something changed; draw again when convenient. */
    virtual void requestRedraw (void) {}

    /* The drawing is now `w' by `h' pixels. */
    virtual void resizeShell (int w, int h) { (void)w; (void)h; }

    /* The visible part of the drawing, in shell pixels: the scroll
       position and the view's size. False if the shell cannot say --
       nothing allocated yet, or no shell. */
    virtual bool shellViewport (double &x, double &y,
                                double &w, double &h) const
    {
        (void)x; (void)y; (void)w; (void)h;
        return false;
    }

    /* Take the keyboard, so keyPressed() is reached. */
    virtual void takeFocus (void) {}

private:
    double zoom_;

    /* Set by zoomToFit when there was no view to fit to; acted on by
       the next shellResized. */
    bool fitPending_;
};

#endif /* CANVAS_CONTENT_H */
