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

#include "GraphCanvas.h"

GraphCanvas::GraphCanvas (CanvasContent &content)
    : content_(content)
{
    set_draw_func(sigc::mem_fun(*this, &GraphCanvas::onDraw));

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
GraphCanvas::onDraw (const Cairo::RefPtr<Cairo::Context> &cr, int width,
                     int height)
{
    content_.draw(cr, width, height);
}

/* The scrolled window this canvas lives in, or NULL. */
static Gtk::ScrolledWindow *
scrollerOf (const Gtk::Widget *w)
{
    Gtk::Widget *p = w ? const_cast<Gtk::Widget *>(w)->get_parent() : NULL;

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
viewportSize (const Gtk::Widget *w, int &cw, int &ch)
{
    cw = ch = 0;

    Gtk::Widget *p = w ? const_cast<Gtk::Widget *>(w)->get_parent() : NULL;

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

bool
GraphCanvas::viewport (double &x, double &y, double &w, double &h) const
{
    x = y = 0;

    Gtk::ScrolledWindow *sw = scrollerOf(this);

    if (sw == NULL)
    {
        /* No scroller: what a canvas built by a harness looks like. The
           widget is the view. */
        w = get_width();
        h = get_height();

        return w >= 1 && h >= 1;
    }

    int cw = 0, ch = 0;

    viewportSize(this, cw, ch);

    if (cw < 1 || ch < 1)
        return false;

    Glib::RefPtr<Gtk::Adjustment> ha = sw->get_hadjustment();
    Glib::RefPtr<Gtk::Adjustment> va = sw->get_vadjustment();

    if (ha)
        x = ha->get_value();

    if (va)
        y = va->get_value();

    w = cw;
    h = ch;

    return true;
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
    content_.shellResized();
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

    content_.setZoom(d < 0.0 ? content_.zoom() * 1.1 : content_.zoom() / 1.1);

    return true;
}
