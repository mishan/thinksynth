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
GraphCanvas::onScroll (double, double dy)
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

    if (dy == 0.0)
        return false;

    setZoom(dy < 0.0 ? zoom_ * 1.1 : zoom_ / 1.1);

    return true;
}
