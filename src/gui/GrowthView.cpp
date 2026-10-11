/*
 * Copyright (C) 2004-2026 The thinksynth authors
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

#include <algorithm>
#include <cmath>

#include "GrowthView.h"

GrowthView::GrowthView (thcScheduler *sched, int channel)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 4), sched_(sched),
      channel_(channel), seen_(0), filled_(false)
{
    caption_.set_xalign(0);
    caption_.set_wrap(true);

    list_.set_selection_mode(Gtk::SelectionMode::NONE);
    list_.signal_row_activated().connect(
        [this] (Gtk::ListBoxRow *row) { choose(row->get_index()); });

    plot_.set_content_height(80);
    plot_.set_draw_func(sigc::mem_fun(*this, &GrowthView::draw));
    plot_.set_visible(false);

    distance_.set_xalign(0);
    distance_.set_visible(false);

    append(caption_);
    append(list_);
    append(plot_);
    append(distance_);

    refill();
}

void
GrowthView::tick (void)
{
    if (!filled_ || sched_->growthSerial() != seen_)
        refill();
}

void
GrowthView::choose (size_t depth)
{
    if (depth + 1 >= stack_.size())
        return;     /* the row playing: nothing to go back to */

    std::string why;

    if (!sched_->growBackTo(channel_, depth, why))
    {
        if (say)
            say(why);
        return;
    }

    if (say)
        say(depth == 0 ? "the declared graph is back on the channel"
                       : "back to " + std::to_string(depth) + " edits");

    refill();
}

void
GrowthView::refill (void)
{
    seen_ = sched_->growthSerial();
    filled_ = true;

    if (!sched_->growth(channel_, stack_, log_))
    {
        stack_.clear();
        log_.clear();
    }

    while (Gtk::Widget *child = list_.get_first_child())
        list_.remove(*child);

    caption_.set_text(stack_.size() < 2 ? "Playing the declared graph."
                                        : "Edits on the declared graph; "
                                          "choose one to go back to it.");

    for (size_t i = 0; i < stack_.size() && stack_.size() > 1; i++)
    {
        const bool playing = i + 1 == stack_.size();
        std::string text = i == 0 ? "the declared graph"
                                  : std::to_string(i) + ". " + stack_[i];

        if (playing)
            text = "<b>" + Glib::Markup::escape_text(text) +
                   "</b>  (playing)";
        else
            text = Glib::Markup::escape_text(text);

        Gtk::Label *label = Gtk::make_managed<Gtk::Label>();

        label->set_markup(text);
        label->set_xalign(0);
        label->set_wrap(true);
        list_.append(*label);
        list_.get_row_at_index((int)i)->set_activatable(!playing);
    }

    double first = NAN, now = NAN;

    for (const thcScheduler::GrowthStep &s : log_)
    {
        if (std::isnan(first))
            first = std::isfinite(s.from) ? s.from : s.distance;

        now = s.distance;
    }

    const bool heard = std::isfinite(first) || std::isfinite(now);

    const auto db = [] (double d)
    {
        return std::isfinite(d) ? std::to_string((int)std::lround(d)) + " dB"
                                : std::string("not heard");
    };

    distance_.set_text("From the target: " + db(now) + " now, " + db(first) +
                       " at first; nearer is higher.");

    plot_.set_visible(heard);
    distance_.set_visible(heard);
    plot_.queue_draw();
}

/* Time across, distance down the page from nearest at the top. */
void
GrowthView::draw (const Cairo::RefPtr<Cairo::Context> &cr, int w, int h)
{
    cr->set_source_rgb(0x14 / 255.0, 0x16 / 255.0, 0x1a / 255.0);
    cr->paint();

    double t0 = HUGE_VAL, t1 = -HUGE_VAL, lo = HUGE_VAL, hi = -HUGE_VAL;
    const auto take = [&] (double t, double d)
    {
        if (!std::isfinite(d))
            return;

        t0 = std::min(t0, t);
        t1 = std::max(t1, t);
        lo = std::min(lo, d);
        hi = std::max(hi, d);
    };

    for (const thcScheduler::GrowthStep &s : log_)
    {
        take(s.at, s.distance);
        take(s.at, s.from);

        for (double d : s.heard)
            take(s.at, d);
    }

    if (t0 > t1)
        return;

    const double pad = 6;
    const double span = std::max(t1 - t0, 1.0);
    const double range = std::max(hi - lo, 0.5);
    const auto x = [&] (double t) { return pad + (t - t0) / span * (w - 2 * pad); };
    const auto y = [&] (double d) { return pad + (d - lo) / range * (h - 2 * pad); };

    /* The edits heard at each step, the one taken filled. */
    for (const thcScheduler::GrowthStep &s : log_)
        for (size_t i = 0; i < s.heard.size(); i++)
        {
            if (!std::isfinite(s.heard[i]))
                continue;

            const bool taken = (int)i == s.taken;

            cr->arc(x(s.at), y(s.heard[i]), taken ? 3.5 : 2.5, 0, 2 * M_PI);

            if (taken)
            {
                cr->set_source_rgb(0.35, 0.65, 1.0);
                cr->fill();
            }
            else
            {
                cr->set_source_rgba(0.8, 0.8, 0.8, 0.5);
                cr->set_line_width(1);
                cr->stroke();
            }
        }

    /* The graph on the channel: where each step started, then where it
       went, held until the next. */
    bool down = false;

    for (const thcScheduler::GrowthStep &s : log_)
    {
        if (std::isfinite(s.from))
        {
            if (down)
                cr->line_to(x(s.at), y(s.from));
            else
                cr->move_to(x(s.at), y(s.from));
            down = true;
        }

        if (std::isfinite(s.distance))
        {
            if (down)
                cr->line_to(x(s.at), y(s.distance));
            else
                cr->move_to(x(s.at), y(s.distance));
            down = true;
        }
        else
            down = false;   /* a graph nobody heard: a gap, not a guess */
    }

    cr->set_source_rgb(0.9, 0.9, 0.9);
    cr->set_line_width(1.5);
    cr->stroke();
}
