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

#include "config.h"

#include <algorithm>

#include "GraphCanvas.h"

/* The same bounds NodeCanvas has always had. A quarter is where a large
 * patch stops being legible and three is where a small one stops gaining
 * anything. */
#define ZOOM_MIN  0.25
#define ZOOM_MAX  3.0

GraphCanvas::GraphCanvas (void)
    : zoom_(1.0), fitPending_(false)
{
    /* BOTH_AXES rather than VERTICAL: a touchpad reports horizontal
       deltas too, and a controller that did not ask for them would let
       them through to the scroller while the vertical ones were being
       eaten here. */
    scroll_ = Gtk::EventControllerScroll::create();
    scroll_->set_flags(Gtk::EventControllerScroll::Flags::BOTH_AXES);
    scroll_->signal_scroll().connect(
        sigc::mem_fun(*this, &GraphCanvas::onScroll), false);
    add_controller(scroll_);

    signal_resize().connect(sigc::mem_fun(*this, &GraphCanvas::onResize));
}

void
GraphCanvas::toContent (double sx, double sy, double &cx, double &cy) const
{
    cx = sx / zoom_;
    cy = sy / zoom_;
}

void
GraphCanvas::contentResized (void)
{
    double w = 0, h = 0;

    contentExtent(w, h);

    if (w > 0 && h > 0)
        set_size_request((int)(w * zoom_), (int)(h * zoom_));
}

void
GraphCanvas::setZoom (double z)
{
    z = std::min(std::max(z, (double)ZOOM_MIN), (double)ZOOM_MAX);

    if (z == zoom_)
        return;

    zoom_ = z;

    contentResized();
    queue_draw();
}

/* The scrolled window this canvas lives in, or NULL. */
static Gtk::ScrolledWindow *
scrollerOf (Gtk::Widget *w)
{
    Gtk::Widget *p = w ? w->get_parent() : NULL;

    while (p)
    {
        Gtk::ScrolledWindow *s = dynamic_cast<Gtk::ScrolledWindow *>(p);

        if (s)
            return s;

        p = p->get_parent();
    }

    return NULL;
}

/* The space available to draw in: the scrolled window's viewport, not
 * this widget, which has already been sized to the content. */
static void
viewportSize (Gtk::Widget *w, int &cw, int &ch)
{
    cw = ch = 0;

    Gtk::Widget *p = w ? w->get_parent() : NULL;

    while (p)
    {
        Gtk::Viewport *v = dynamic_cast<Gtk::Viewport *>(p);

        if (v)
        {
            cw = v->get_allocated_width();
            ch = v->get_allocated_height();
            return;
        }

        p = p->get_parent();
    }
}

/* What can actually be seen, in the subclass's own coordinates.
 *
 * Not the widget's size: the widget is sized to the whole scaled
 * drawing, so get_width() on a canvas in a scroller is the width of
 * everything, scrolled off or not. Anything that wants to fill the
 * *view* -- the composer's enlarged stage, a future overlay -- has to
 * ask for the viewport and where it currently sits, or it will lay
 * itself out across the content and be somewhere else the moment
 * anybody scrolls.
 *
 * Falls back to the widget when there is no scroller, which is what a
 * canvas built by a harness looks like. */
void
GraphCanvas::visibleRect (double &x, double &y, double &w, double &h) const
{
    x = y = 0;
    w = get_width() / zoom_;
    h = get_height() / zoom_;

    Gtk::ScrolledWindow *sw = scrollerOf(const_cast<GraphCanvas *>(this));

    if (sw == NULL)
        return;

    int cw = 0, ch = 0;

    viewportSize(const_cast<GraphCanvas *>(this), cw, ch);

    if (cw < 1 || ch < 1)
        return;

    Glib::RefPtr<Gtk::Adjustment> ha = sw->get_hadjustment();
    Glib::RefPtr<Gtk::Adjustment> va = sw->get_vadjustment();

    if (ha)
        x = ha->get_value() / zoom_;

    if (va)
        y = va->get_value() / zoom_;

    w = cw / zoom_;
    h = ch / zoom_;
}

void
GraphCanvas::zoomToFit (void)
{
    double gw = 0, gh = 0;

    contentExtent(gw, gh);

    if (gw <= 0 || gh <= 0)
        return;

    int cw = 0, ch = 0;

    viewportSize(this, cw, ch);

    if (cw < 32 || ch < 32)
    {
        /* Nothing allocated yet -- this is the first open, before GTK
           has laid anything out. Try again when it has. */
        fitPending_ = true;
        return;
    }

    fitPending_ = false;

    double z = std::min(cw / gw, ch / gh);

    if (z > 1.0)
        z = 1.0;

    setZoom(z);

    /* setZoom returns early when the zoom did not change, which on a
       first fit of a drawing that already fits is exactly what happens
       -- and the size request still has to be made. */
    contentResized();
}

/* A deferred fit, taken the moment the canvas has a size to fit to.
 *
 * on_size_allocate is not overridable in GTK4 -- the vfunc is
 * size_allocate with a different signature, and overriding it means
 * taking responsibility for allocating the children too. signal_resize
 * says the same thing and asks for nothing. */
void
GraphCanvas::onResize (int, int)
{
    if (fitPending_)
        zoomToFit();
}

bool
GraphCanvas::onScroll (double dx, double dy)
{
    /* Ctrl+wheel zooms; a bare wheel is left to the scrolled window,
     * which is what people expect of a large canvas.
     *
     * The modifier comes off the controller's current event rather than
     * out of a struct member -- and a controller reports every wheel as
     * a delta, so the discrete up/down cases and the smooth one are one
     * case. */
    if ((scroll_->get_current_event_state() & Gdk::ModifierType::CONTROL_MASK)
        != Gdk::ModifierType::CONTROL_MASK)
        return false;

    /* Either axis. The controller was asked for BOTH_AXES so that a
       touchpad's sideways deltas do not leak past it to the scroller,
       and then this looked at dy alone: a Ctrl-held horizontal scroll
       fell through and *panned*, which is the one thing holding Ctrl
       was meant to stop. A wheel is dy and a touchpad's sideways swipe
       is dx, and with Ctrl down both mean the same thing. */
    const double d = dy != 0.0 ? dy : dx;

    if (d == 0.0)
        return true;      /* Ctrl was held: eaten either way            */

    setZoom(d < 0.0 ? zoom_ * 1.1 : zoom_ / 1.1);

    return true;
}
