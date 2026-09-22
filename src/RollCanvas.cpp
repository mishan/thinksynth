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

#include "RollCanvas.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "gui-util.h"

static const double CHANARG_STRIP = 26;   /* px reserved at the bottom   */

/* Reserved at the top for structure edits. Small: they are sparse by
 * construction -- being events is what rate-limits them -- so a lane
 * that fits a line of text is a lane that fits a piece's worth. */
static const double EDIT_LANE = 12;
static const double EASE          = 0.12; /* pitch-range easing per frame*/

/* The now-line sits at 2/3 width because the default spans are 60s of
 * past to 30s of future -- its position IS that ratio, so there is no
 * separate constant to fall out of step with it. */

/* The bounds a span is kept inside, wherever it is set from: timeToX
 * divides by the sum, and a zero or negative span is a request for a
 * crash and not a view. */
static const double PAST_MIN = 5.0,   PAST_MAX = 600.0;
static const double FUTURE_MIN = 2.5, FUTURE_MAX = 300.0;

/* Channel hues live in gui-util.h, shared with the composer canvas whose
 * sink boxes wear the same color this gives the channel's notes. One
 * table, or the boxes and the notes drift apart. */
static void
channelColor (const Cairo::RefPtr<Cairo::Context> &cr, int chan,
              double alpha)
{
    double r, g, b;

    gthChannelColor(chan, r, g, b);
    cr->set_source_rgba(r, g, b, alpha);
}

RollCanvas::RollCanvas (thcScheduler *sched)
    : sched_(sched), spanPast_(60), spanFuture_(30), viewNow_(0),
      following_(true), dragging_(false), dragX0_(0), dragT0_(0),
      loShown_(48), hiShown_(72), loFit_(48), hiFit_(72),
      lastW_(0), lastH_(0)
{
    deliveredConn_ = sched_->sigDelivered.connect(
        sigc::mem_fun(*this, &RollCanvas::onDelivered));

    resetConn_ = sched_->sigReset.connect(
        sigc::mem_fun(*this, &RollCanvas::onTransportReset));
}

RollCanvas::~RollCanvas (void)
{
    deliveredConn_.disconnect();
    resetConn_.disconnect();
}

void
RollCanvas::onDelivered (const thcEvent &ev)
{
    /* The event's own timestamp, not the scheduler's now: delivery
       runs on a ~20ms tick and a humanized note's `at' is the point of
       it -- drawing arrival times would shift every bar by delivery
       latency and render a replayed stream differently from its
       authored self. */
    if (ev.type == THC_EV_NOTE)
    {
        const Note n = { ev.at, ev.u.note.duration, ev.channel,
                         ev.u.note.note, ev.u.note.velocity };

        /* A duration <= 0 is live input's "held until further notice",
           and a bar with no end yet is not history: it belongs in
           held_ until a NOTEOFF says where it stops. */
        if (n.duration > 0)
            notes_.push_back(n);
        else
            held_.push_back(n);
    }
    else if (ev.type == THC_EV_NOTEOFF)
    {
        /* The release live input promised: give the held bar its real
           end and move it across into history, where prune() can reach
           it. The scheduler sends one of these for every held note --
           on the release, and on the flush a stop or a chain swap does
           (thcScheduler::flushHeld) -- so nothing is left open here
           that is no longer sounding. */
        for (size_t i = held_.size(); i-- > 0; )
            if (held_[i].channel == ev.channel &&
                held_[i].note == ev.u.note.note)
            {
                Note n = held_[i];

                n.duration = std::max(ev.at - n.start, 0.05);
                notes_.push_back(n);
                held_.erase(held_.begin() + i);
                break;
            }
    }
    else if (ev.type == THC_EV_CHANARG)
    {
        /* Normalized by the arg's declared range where it has one --
           the honest scale the strip always wanted; 0-1 stays the
           fallback for an arg that never said. */
        float lo, hi;
        double v = ev.u.chanarg.value;

        if (sched_->chanArgRange(ev.channel, ev.u.chanarg.name, lo, hi))
            v = (v - lo) / (hi - lo);

        argTicks_.push_back({ ev.at, ev.channel, (float)v });
    }
    else if (ev.type == THC_EV_PATCH || ev.type == THC_EV_NODEARG)
    {
        /* The label is what the piece said, not what the host made of
           it: "bell", or "fmap.inmax". Somebody reading the roll is
           looking for the line in the file that caused this. */
        std::string label;

        if (ev.type == THC_EV_PATCH)
            label = ev.u.patch.name ? ev.u.patch.name : "?";
        else
        {
            char buf[96];

            snprintf(buf, sizeof(buf), "%s.%s %.3g",
                     ev.u.nodearg.node ? ev.u.nodearg.node : "?",
                     ev.u.nodearg.arg ? ev.u.nodearg.arg : "?",
                     (double)ev.u.nodearg.value);
            label = buf;
        }

        edits_.push_back({ ev.at, ev.channel, label });
    }
    /* Pruned here rather than once a frame, which is where it used to be
       when this was a widget that only existed while it was on screen.
       A pane that is folded away stops asking for frames -- that is the
       point of asking -- and the scheduler goes on delivering into it,
       so a prune that only ran inside a draw was an unbounded history
       for as long as nobody was looking. It is a pop_front or none.

       No requestRedraw: the shell repaints every frame anyway. */
    prune();
}

/* The transport rewound: history keyed to the old timeline is now a
 * lie. Kept notes would sit *right* of the new now-line and draw as a
 * future that already happened -- which is exactly what showed up when
 * switching pieces, the previous piece's traces refusing to leave
 * (prune() could never reach them either: its cutoff is now minus four
 * spans, which at a fresh zero is negative and keeps everything). */
void
RollCanvas::onTransportReset (void)
{
    clear();
}

void
RollCanvas::clear (void)
{
    notes_.clear();
    held_.clear();
    argTicks_.clear();
    edits_.clear();
    pendingView_.clear();
    viewNow_ = 0;
    following_ = true;
    dragging_ = false;
    requestRedraw();
}

void
RollCanvas::step (void)
{
    if (following_)
        viewNow_ = sched_->now();

    /* One copy of the scheduled future per frame, shared by the range
       fit and the draw -- peekPending rebuilds its vector per call, and
       asking twice a frame was paying for the copy twice. */
    pendingView_ = sched_->peekPending();

    /* And put in time order, which peekPending's is not: what it hands
       back mirrors a heap, and how a heap lays itself out is the standard
       library's to choose -- libstdc++ and libc++ choose differently,
       which is the same split the scheduler's own LaterPending exists to
       close. The order of two ghosts that overlap is nothing anybody can
       see, and being a function of which library this was built with is
       the difference between "one class draws one picture" and a claim
       nothing can check. rollcheck is what checks it. */
    std::sort(pendingView_.begin(), pendingView_.end(),
              [](const thcEvent &a, const thcEvent &b)
              {
                  if (a.at != b.at)
                      return a.at < b.at;

                  if (a.channel != b.channel)
                      return a.channel < b.channel;

                  if (a.type != b.type)
                      return a.type < b.type;

                  /* Same type, so the union's note arm is the one both
                     of them have -- and all of it, because all of it is
                     drawn. Two ghosts at one instant on one channel at
                     one pitch still draw different bars if their lengths
                     differ, and a tie-break that stopped at the pitch
                     would leave std::sort (which is not stable) free to
                     order them by whichever library built it. That is
                     the exact split this sort exists to close, and the
                     one rollcheck.mjs would report as the two builds
                     disagreeing. Velocity is the alpha, so it counts
                     too; anything past it draws the same mark in the
                     same place. */
                  if (a.type != THC_EV_NOTE)
                      return false;

                  if (a.u.note.note != b.u.note.note)
                      return a.u.note.note < b.u.note.note;

                  if (a.u.note.duration != b.u.note.duration)
                      return a.u.note.duration < b.u.note.duration;

                  return a.u.note.velocity < b.u.note.velocity;
              });

    fitPitchRange();
}

/* History older than the widest span anyone could scrub to (plus slack)
 * goes away. deque + pop_front, done. Scrub range is capped at 4x the
 * visible span so "look back" has an honest limit instead of an
 * unbounded buffer pretending to be one.
 *
 * Every bar in notes_ has an end -- one that is still sounding waits in
 * held_ (RollCanvas.h) -- so nothing here can be un-prunable, and the
 * walk cannot be stopped at the front by a note that never ends. */
void
RollCanvas::prune (void)
{
    double keep = sched_->now() - 4 * spanPast_;

    while (!notes_.empty() &&
           notes_.front().start + notes_.front().duration < keep)
        notes_.pop_front();

    while (!argTicks_.empty() && argTicks_.front().at < keep)
        argTicks_.pop_front();

    while (!edits_.empty() && edits_.front().at < keep)
        edits_.pop_front();
}

/* Fit the lane range to what is on screen, ease the shown range toward
 * it. Floor of an octave so a one-note piece does not become one giant
 * bar; a lane of padding each side so nothing touches the edge. */
void
RollCanvas::fitPitchRange (void)
{
    int lo = 127, hi = 0;
    double left = viewNow_ - spanPast_, right = viewNow_ + spanFuture_;

    for (const Note &n : notes_)
        if (noteEnd(n) >= left && n.start <= right)
        {
            lo = std::min(lo, n.note);
            hi = std::max(hi, n.note);
        }

    for (const Note &n : held_)
        if (noteEnd(n) >= left && n.start <= right)
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

/* For a harness; RollCanvas.h says why the drawing cannot answer it. */
double
RollCanvas::oldestKept (void) const
{
    return notes_.empty() ? sched_->now()
                          : notes_.front().start + notes_.front().duration;
}

/* Both callers' one answer for where a bar stops; RollCanvas.h says why
   it has to be one. */
double
RollCanvas::noteEnd (const Note &n) const
{
    return n.duration > 0 ? n.start + n.duration
                          : std::max(viewNow_, n.start + 0.05);
}

double
RollCanvas::timeToX (double t, int width) const
{
    double pxPerSec = width / (spanPast_ + spanFuture_);

    return (t - (viewNow_ - spanPast_)) * pxPerSec;
}

/* The roll's drawing is always exactly its view, which is what pins the
   zoom at 1 and makes a fit a no-op. RollCanvas.h says why that is the
   right shape for this one canvas. */
void
RollCanvas::contentExtent (double &w, double &h) const
{
    double x = 0, y = 0, vw = 0, vh = 0;

    if (shellViewport(x, y, vw, vh) && vw >= 1 && vh >= 1)
    {
        w = vw;
        h = vh;
        return;
    }

    /* No shell to ask, which is what a canvas built by a harness looks
       like: the last picture drawn is the view there. Zero before even
       that means "nothing to show", and the base then leaves the size
       and the zoom alone. */
    w = lastW_;
    h = lastH_;
}

double
RollCanvas::shellWidth (void) const
{
    double w = 0, h = 0;

    contentExtent(w, h);

    return w;
}

void
RollCanvas::draw (const Cairo::RefPtr<Cairo::Context> &cr, int width,
                  int height)
{
    step();

    lastW_ = width;
    lastH_ = height;

    /* The roll is what is left between the two reserved bands, and the
       pitch mapping is offset past the top one. Reserving a lane by
       naming a constant and then drawing the notes over it is how the
       edit labels came to sit on top of the highest pitches. */
    double rollH = std::max(height - CHANARG_STRIP - EDIT_LANE, 1.0);
    double lanes = hiShown_ - loShown_;
    double laneH = rollH / lanes;
    auto   noteY = [&](double n) {
        return EDIT_LANE + rollH - (n - loShown_) * laneH;
    };

    cr->set_source_rgb(0.09, 0.09, 0.11);
    cr->paint();

    /* Octave shading and C gridlines -- the black-key rows get a slightly
       lighter wash so pitch is readable without labels.
     *
       Clipped to the roll, because the loop deliberately runs a row past
       each end so a partly-visible lane is still shaded, and the reserved
       lane is only reserved if the wash stops at it. */
    cr->save();
    cr->rectangle(0, EDIT_LANE, width, rollH);
    cr->clip();

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

    cr->restore();

    /* Delivered notes: filled, alpha from velocity. The tail a patch's
       release adds after note-off is unknowable here -- the scheduler
       sees durations, not envelopes -- so bars end honestly at the off.
       The ended ones and then the ones still sounding, whose bars grow
       to the now-line (noteEnd). */
    auto bar = [&](const Note &n) {
        double x0 = timeToX(n.start, width);
        double x1 = timeToX(noteEnd(n), width);

        if (x1 < 0 || x0 > width)
            return;

        channelColor(cr, n.channel, 0.35 + 0.65 * (n.velocity / 127.0));
        cr->rectangle(x0, noteY(n.note + 1) + 1,
                      std::max(x1 - x0, 2.0), laneH - 2);
        cr->fill();
    };

    for (const Note &n : notes_)
        bar(n);

    for (const Note &n : held_)
        bar(n);

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
       Normalized by the arg's declared range where it has one, and 0-1
       where it has not (onDelivered). */
    for (const ArgTick &a : argTicks_)
    {
        double x = timeToX(a.at, width);

        if (x < 0 || x > width)
            continue;

        /* Clamped: an arg that never declared a range is normalized as
           0-1 on an assumption, and a knob with a wider one must not
           draw outside its reserved band. */
        double v = std::clamp((double)a.value, 0.0, 1.0);
        double y = height - 3 - v * (CHANARG_STRIP - 8);

        channelColor(cr, a.channel, 0.9);
        cr->move_to(x, y - 3); cr->line_to(x + 3, y);
        cr->line_to(x, y + 3); cr->line_to(x - 3, y);
        cr->close_path();
        cr->fill();
    }

    /* Structure edits: a tick and its label along the top.
     *
     * Its own lane rather than a mark in the roll, because an edit is
     * not a pitch and has nowhere to sit among them -- and because the
     * point of drawing one is to see it *coming*, against the notes it
     * is about to change the sound of. Text, since a swap has no value
     * to plot: "bell" is the whole of what happened. */
    for (const Edit &e : edits_)
    {
        double x = timeToX(e.at, width);

        if (x < 0 || x > width)
            continue;

        channelColor(cr, e.channel, 0.95);
        cr->set_line_width(1);
        cr->move_to(x + 0.5, 0);
        cr->line_to(x + 0.5, EDIT_LANE);
        cr->stroke();

        cr->set_font_size(9);
        cr->move_to(x + 3, EDIT_LANE - 3);
        cr->show_text(e.label);
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
RollCanvas::pressAt (double x, double, int, int nPress)
{
    dragging_ = true;
    dragX0_ = x;
    dragT0_ = viewNow_;

    /* A double-click anywhere on the roll goes back to live, which is
       the way out of a scrub that does not require finding the edge. */
    if (nPress == 2)
        following_ = true;
}

void
RollCanvas::motionTo (double x, double)
{
    if (!dragging_)
        return;                 /* a pointer crossing, not a scrub      */

    /* A shell with no size yet answers zero for its width, and a scrub
       through a division by zero lands the view on NaN forever. */
    double width = shellWidth();

    if (width <= 0)
        return;

    double pxPerSec = width / (spanPast_ + spanFuture_);

    following_ = false;
    viewNow_ = dragT0_ - (x - dragX0_) / pxPerSec;

    /* scrub honesty: can't look further back than we kept, and scrubbing
       up to (or past) live snaps back into follow mode */
    double now = sched_->now();

    viewNow_ = std::max(viewNow_, now - 4 * spanPast_);

    if (viewNow_ >= now)
    {
        viewNow_ = now;
        following_ = true;
    }

    requestRedraw();
}

void
RollCanvas::releaseAt (double, double, int)
{
    dragging_ = false;
}

void
RollCanvas::zoomBy (double factor)
{
    /* A factor of one is a report, not a request; a factor of zero is a
       shell with a bug and a division waiting to happen. */
    if (!(factor > 0) || factor == 1.0)
        return;

    /* Divided, not multiplied, and that is the whole of the wart. What a
       shell means by `zoomBy' is how much bigger to draw the drawing,
       and the roll's answer to being drawn bigger is to show *less*
       time -- the same notch that magnifies a patch's graph is the notch
       that brings a bar closer here. So the factor arrives as a scale
       and is spent as a span, the two spans together so the past:future
       ratio the now-line's position is survives it. */
    spanPast_   = std::clamp(spanPast_ / factor, PAST_MIN, PAST_MAX);
    spanFuture_ = std::clamp(spanFuture_ / factor, FUTURE_MIN, FUTURE_MAX);

    requestRedraw();
}

void
RollCanvas::SetTimeSpan (double past, double future)
{
    spanPast_ = std::clamp(past, PAST_MIN, PAST_MAX);
    spanFuture_ = std::clamp(future, FUTURE_MIN, FUTURE_MAX);
}
