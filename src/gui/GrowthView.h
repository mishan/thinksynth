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

#ifndef GROWTH_VIEW_H
#define GROWTH_VIEW_H

#include <functional>
#include <string>
#include <vector>

#include <gtkmm.h>

#include "thcScheduler.h"

/* What gen::grow has made of one channel's graph, under its stage in the
 * Composer's Selection pane: the edits on the declared graph in words,
 * the one playing last, and with a target, how far each graph was heard
 * from it over time -- the edits heard at each step as dots, the one
 * taken filled, and the graph on the channel as a line, nearer higher.
 *
 * Choosing a row puts that graph back on the channel, as steps back
 * would, and Keep then writes it. Read from the scheduler only when its
 * growth has moved, so a tick costs a comparison.
 */
class GrowthView : public Gtk::Box
{
public:
    GrowthView (thcScheduler *sched, int channel);

    /* The Composer's draw timer, while the view is up. */
    void tick (void);

    /* What a row's press did, for the status line. */
    std::function<void (const std::string &)> say;

    /* The rows as shown, for a harness: the declared graph's first. */
    const std::vector<std::string> &rows (void) const { return stack_; }

    /* Presses row `depth', as a click does. */
    void choose (size_t depth);

private:
    void refill (void);
    void draw (const Cairo::RefPtr<Cairo::Context> &cr, int w, int h);

    thcScheduler *sched_;
    int channel_;
    unsigned int seen_;
    bool filled_;

    Gtk::Label caption_;
    Gtk::ListBox list_;
    Gtk::DrawingArea plot_;
    Gtk::Label distance_;

    std::vector<std::string> stack_;
    std::vector<thcScheduler::GrowthStep> log_;
};

#endif /* GROWTH_VIEW_H */
