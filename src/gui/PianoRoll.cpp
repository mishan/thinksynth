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
 */

#include "config.h"

#include "PianoRoll.h"

PianoRoll::PianoRoll (thcScheduler *sched)
    : RollCanvas(sched)
{
    set_draw_func(sigc::mem_fun(*this, &PianoRoll::onDraw));

    /* A scrolling view redraws every frame while visible; the frame
       clock is the right driver for that, not a Glib timeout guessing
       at the compositor's rate. queue_draw from delivery alone would
       leave the scroll advancing in event-sized lurches. */
    add_tick_callback(sigc::mem_fun(*this, &PianoRoll::onTick));

    /* A click and a motion rather than a Gtk::GestureDrag, so both
       shells reach the canvas through the same three calls: the drag
       gesture reports an offset and a browser reports a position, and
       the content takes positions (ComposerCanvasWidget does the same
       for the same reason). */
    auto click = Gtk::GestureClick::create();

    click->set_button(GDK_BUTTON_PRIMARY);
    click->signal_pressed().connect(
        [this](int n, double x, double y) { pressAt(x, y, 1, n); });
    click->signal_released().connect(
        [this](int, double x, double y) { releaseAt(x, y, 1); });
    add_controller(click);

    auto motion = Gtk::EventControllerMotion::create();

    motion->signal_motion().connect(
        [this](double x, double y) { motionTo(x, y); });
    add_controller(motion);

    auto scroll = Gtk::EventControllerScroll::create();

    scroll->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL);
    scroll->signal_scroll().connect(
        sigc::mem_fun(*this, &PianoRoll::onScroll), false);
    add_controller(scroll);
}

PianoRoll::~PianoRoll (void)
{
    /* No remove_tick_callback here, deliberately: for a managed child
       the C++ destructor runs after GTK has disposed the widget, when
       the tick callback is already gone and the call is an assertion
       failure on a dead GObject. GTK removes frame-clock callbacks at
       dispose; the scheduler's connections are what GTK does not know
       about, and RollCanvas drops those itself. */
}

/* The widget is the view: there is no scroller to ask and nothing is
   ever scrolled off. */
bool
PianoRoll::shellViewport (double &x, double &y, double &w, double &h) const
{
    x = y = 0;
    w = get_width();
    h = get_height();

    return w >= 1 && h >= 1;
}

void
PianoRoll::onDraw (const Cairo::RefPtr<Cairo::Context> &cr, int width,
                   int height)
{
    draw(cr, width, height);
}

bool
PianoRoll::onTick (const Glib::RefPtr<Gdk::FrameClock> &)
{
    /* The draw is where the frame is taken (RollCanvas::draw), so this
       is the whole of what the clock is for. */
    queue_draw();

    return true;
}

bool
PianoRoll::onScroll (double, double dy)
{
    /* A plain wheel, and no Ctrl: the roll has nothing to scroll, so
       there is nothing for a bare wheel to be left to. Down is out, and
       the factor is how much bigger to draw -- which RollCanvas spends
       as a span. */
    if (dy == 0)
        return false;           /* a report, not a request              */

    zoomBy(dy > 0 ? 0.8 : 1.25);

    return true;
}
