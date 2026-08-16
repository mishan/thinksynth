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

#include <algorithm>
#include <cmath>

static const double CHANARG_STRIP = 26;   /* px reserved at the bottom   */
static const double EASE          = 0.12; /* pitch-range easing per frame*/

/* The now-line sits at 2/3 width because the default spans are 60s of
 * past to 30s of future -- its position IS that ratio, so there is no
 * separate constant to fall out of step with it. */

/* One color per MIDI channel, hue-spaced by the golden angle. Matches
 * nothing else in the app yet; if channel colors grow legs (patch
 * selector, keyboard), this table moves to ColumnUtil and everyone
 * shares it.
 *
 * The HSV->RGB conversion is spelled out here because gtkmm-4 has
 * nowhere to borrow it from: Gtk::HSV went with GTK3 and Gdk::RGBA only
 * parses names. Saturation and value are fixed, so this is the h-sector
 * dance and nothing more. */
static void
channelColor (const Cairo::RefPtr<Cairo::Context> &cr, int chan,
              double alpha)
{
    double h = (chan * 137.508) / 360.0;      /* golden-angle spacing    */
    h -= std::floor(h);

    const double s = 0.65, v = 0.85;

    double f = h * 6.0;
    int    sector = (int)f % 6;
    f -= std::floor(f);

    double p = v * (1 - s);
    double q = v * (1 - s * f);
    double t = v * (1 - s * (1 - f));
    double r, g, b;

    switch (sector)
    {
        case 0:  r = v; g = t; b = p; break;
        case 1:  r = q; g = v; b = p; break;
        case 2:  r = p; g = v; b = t; break;
        case 3:  r = p; g = q; b = v; break;
        case 4:  r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }

    cr->set_source_rgba(r, g, b, alpha);
}

PianoRoll::PianoRoll (thcScheduler *sched)
    : sched_(sched), spanPast_(60), spanFuture_(30), viewNow_(0),
      following_(true), loShown_(48), hiShown_(72), loFit_(48), hiFit_(72)
{
    set_draw_func(sigc::mem_fun(*this, &PianoRoll::onDraw));

    deliveredConn_ = sched_->sigDelivered.connect(
        sigc::mem_fun(*this, &PianoRoll::onDelivered));

    /* A scrolling view redraws every frame while visible; the frame
       clock is the right driver for that, not a Glib timeout guessing
       at the compositor's rate. queue_draw from delivery alone would
       leave the scroll advancing in event-sized lurches. */
    tickId_ = add_tick_callback(sigc::mem_fun(*this, &PianoRoll::onTick));

    auto drag = Gtk::GestureDrag::create();
    drag->signal_drag_begin().connect(
        sigc::mem_fun(*this, &PianoRoll::onDragBegin));
    drag->signal_drag_update().connect(
        sigc::mem_fun(*this, &PianoRoll::onDragUpdate));
    add_controller(drag);

    auto scroll = Gtk::EventControllerScroll::create();
    scroll->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL);
    scroll->signal_scroll().connect(
        sigc::mem_fun(*this, &PianoRoll::onScroll), false);
    add_controller(scroll);

    auto click = Gtk::GestureClick::create();
    click->set_button(GDK_BUTTON_PRIMARY);
    click->signal_pressed().connect(
        sigc::mem_fun(*this, &PianoRoll::onDoubleClick));
    add_controller(click);
}

PianoRoll::~PianoRoll (void)
{
    deliveredConn_.disconnect();

    /* No remove_tick_callback here, deliberately: for a managed child
       the C++ destructor runs after GTK has disposed the widget, when
       the tick callback is already gone and the call is an assertion
       failure on a dead GObject. GTK removes frame-clock callbacks at
       dispose; the sigc connection above is the one thing GTK does not
       know about. */
}

void
PianoRoll::onDelivered (const thcEvent &ev)
{
    /* The event's own timestamp, not the scheduler's now: delivery
       runs on a ~20ms tick and a humanized note's `at' is the point of
       it -- drawing arrival times would shift every bar by delivery
       latency and render a replayed stream differently from its
       authored self. */
    if (ev.type == THC_EV_NOTE)
        notes_.push_back({ ev.at, ev.u.note.duration,
                           ev.channel, ev.u.note.note,
                           ev.u.note.velocity });
    else
        argTicks_.push_back({ ev.at, ev.channel,
                              ev.u.chanarg.value });
    /* no queue_draw: the tick callback repaints every frame anyway */
}

bool
PianoRoll::onTick (const Glib::RefPtr<Gdk::FrameClock> &)
{
    if (following_)
        viewNow_ = sched_->now();

    /* One copy of the scheduled future per frame, shared by the range
       fit and the draw -- peekPending rebuilds its vector per call, and
       asking twice a frame was paying for the copy twice. */
    pendingView_ = sched_->peekPending();

    prune();
    fitPitchRange();
    queue_draw();

    return true;
}

/* History older than the widest span anyone could scrub to (plus slack)
 * goes away. deque + pop_front, ordered by delivery, done. Scrub range
 * is capped at 4x the visible span so "look back" has an honest limit
 * instead of an unbounded buffer pretending to be one. */
void
PianoRoll::prune (void)
{
    double keep = sched_->now() - 4 * spanPast_;

    while (!notes_.empty() &&
           notes_.front().start + notes_.front().duration < keep)
        notes_.pop_front();

    while (!argTicks_.empty() && argTicks_.front().at < keep)
        argTicks_.pop_front();
}

/* Fit the lane range to what is on screen, ease the shown range toward
 * it. Floor of an octave so a one-note piece does not become one giant
 * bar; a lane of padding each side so nothing touches the edge. */
void
PianoRoll::fitPitchRange (void)
{
    int lo = 127, hi = 0;
    double left = viewNow_ - spanPast_, right = viewNow_ + spanFuture_;

    for (const Note &n : notes_)
        if (n.start + n.duration >= left && n.start <= right)
        {
            lo = std::min(lo, n.note);
            hi = std::max(hi, n.note);
        }

    for (const auto &p : pendingView_)
        if (p.type == THC_EV_NOTE && p.at <= right)
        {
            lo = std::min(lo, p.u.note.note);
            hi = std::max(hi, p.u.note.note);
        }

    if (lo > hi) { lo = 57; hi = 69; }          /* empty: A3..A4        */

    while (hi - lo < 12) { if (lo > 0) lo--; if (hi < 127) hi++; }

    loFit_ = lo - 1;
    hiFit_ = hi + 1;

    loShown_ += (loFit_ - loShown_) * EASE;
    hiShown_ += (hiFit_ - hiShown_) * EASE;
}

double
PianoRoll::timeToX (double t, int width) const
{
    double pxPerSec = width / (spanPast_ + spanFuture_);

    return (t - (viewNow_ - spanPast_)) * pxPerSec;
}

void
PianoRoll::onDraw (const Cairo::RefPtr<Cairo::Context> &cr, int width,
                   int height)
{
    double rollH = height - CHANARG_STRIP;
    double lanes = hiShown_ - loShown_;
    double laneH = rollH / lanes;
    auto   noteY = [&](double n) { return rollH - (n - loShown_) * laneH; };

    cr->set_source_rgb(0.09, 0.09, 0.11);
    cr->paint();

    /* Octave shading and C gridlines -- the black-key rows get a slightly
       lighter wash so pitch is readable without labels. */
    for (int n = (int)loShown_; n <= (int)hiShown_ + 1; n++)
    {
        int pc = ((n % 12) + 12) % 12;
        bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;

        if (black)
        {
            cr->set_source_rgba(1, 1, 1, 0.04);
            cr->rectangle(0, noteY(n + 1), width, laneH);
            cr->fill();
        }

        if (pc == 0)
        {
            cr->set_source_rgba(1, 1, 1, 0.10);
            cr->move_to(0, noteY(n));
            cr->line_to(width, noteY(n));
            cr->set_line_width(1);
            cr->stroke();
        }
    }

    /* Delivered notes: filled, alpha from velocity. The tail a patch's
       release adds after note-off is unknowable here -- the scheduler
       sees durations, not envelopes -- so bars end honestly at the off. */
    for (const Note &n : notes_)
    {
        double x0 = timeToX(n.start, width);
        double x1 = timeToX(n.start + n.duration, width);

        if (x1 < 0 || x0 > width)
            continue;

        channelColor(cr, n.channel, 0.35 + 0.65 * (n.velocity / 127.0));
        cr->rectangle(x0, noteY(n.note + 1) + 1,
                      std::max(x1 - x0, 2.0), laneH - 2);
        cr->fill();
    }

    /* Scheduled future: outline only. peekPending is what falls out of
       the chains and has not been delivered yet -- the piece's actual
       near future, not a prediction. */
    for (const auto &p : pendingView_)
    {
        if (p.type != THC_EV_NOTE)
            continue;

        double x0 = timeToX(p.at, width);
        double x1 = timeToX(p.at + p.u.note.duration, width);

        if (x1 < 0 || x0 > width)
            continue;

        channelColor(cr, p.channel, 0.55);
        cr->set_line_width(1);
        cr->rectangle(x0 + 0.5, noteY(p.u.note.note + 1) + 1.5,
                      std::max(x1 - x0, 2.0) - 1, laneH - 3);
        cr->stroke();
    }

    /* chanarg strip: one diamond per event, value = height in strip.
       Normalized 0-1 for now; the honest range is the arg's declared
       .min/.max, once param metadata is reachable from here (noted in
       the handoff as a known gap). */
    for (const ArgTick &a : argTicks_)
    {
        double x = timeToX(a.at, width);

        if (x < 0 || x > width)
            continue;

        /* Clamped: the strip normalizes 0-1 (a documented stopgap until
           arg metadata is reachable from here), and a knob with a wider
           range must not draw outside its reserved band. */
        double v = std::clamp((double)a.value, 0.0, 1.0);
        double y = height - 3 - v * (CHANARG_STRIP - 8);

        channelColor(cr, a.channel, 0.9);
        cr->move_to(x, y - 3); cr->line_to(x + 3, y);
        cr->line_to(x, y + 3); cr->line_to(x - 3, y);
        cr->close_path();
        cr->fill();
    }

    /* the now-line, and a dimming wash over the not-yet half */
    double nowX = timeToX(viewNow_, width);

    cr->set_source_rgba(0, 0, 0, 0.25);
    cr->rectangle(nowX, 0, width - nowX, height);
    cr->fill();
    cr->set_source_rgba(1.0, 0.85, 0.3, following_ ? 0.9 : 0.5);
    cr->set_line_width(1);
    cr->move_to(nowX, 0);
    cr->line_to(nowX, height);
    cr->stroke();
}

void
PianoRoll::onDragBegin (double, double)
{
    dragT0_ = viewNow_;
}

void
PianoRoll::onDragUpdate (double dx, double)
{
    /* An unallocated widget answers zero for its width, and a scrub
       through a division by zero lands the view on NaN forever. */
    if (get_width() <= 0)
        return;

    double pxPerSec = get_width() / (spanPast_ + spanFuture_);

    following_ = false;
    viewNow_ = dragT0_ - dx / pxPerSec;

    /* scrub honesty: can't look further back than we kept, and scrubbing
       up to (or past) live snaps back into follow mode */
    double now = sched_->now();

    viewNow_ = std::max(viewNow_, now - 4 * spanPast_);

    if (viewNow_ >= now)
    {
        viewNow_ = now;
        following_ = true;
    }
}

bool
PianoRoll::onScroll (double, double dy)
{
    /* zoom time, keeping the past:future ratio; clamp to sane spans.
       A zero delta is a report, not a request. */
    if (dy == 0)
        return false;

    double f = dy > 0 ? 1.25 : 0.8;

    spanPast_   = std::clamp(spanPast_ * f, 5.0, 600.0);
    spanFuture_ = std::clamp(spanFuture_ * f, 2.5, 300.0);

    return true;
}

void
PianoRoll::onDoubleClick (int nPress, double, double)
{
    if (nPress == 2)
        following_ = true;
}

void
PianoRoll::SetTimeSpan (double past, double future)
{
    /* The same bounds the scroll wheel obeys: timeToX divides by the
       sum, and a zero or negative span is a request for a crash, not a
       view. */
    spanPast_ = std::clamp(past, 5.0, 600.0);
    spanFuture_ = std::clamp(future, 2.5, 300.0);
}
