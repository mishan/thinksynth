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

#ifndef MIDI_ROUTES_VIEW_H
#define MIDI_ROUTES_VIEW_H

#include <functional>
#include <vector>

#include <gtkmm.h>

#include "thMidiRoutes.h"

class gthPrefs;

/* This machine's live MIDI input routes, as rows to edit: a MIDI channel,
 * the engine channel it plays (or off), and the keys from low to high. Each
 * edit hands the whole table to `changed', which is where it goes live and
 * is kept. A route here outranks a piece's `midiin' for its MIDI channel.
 */
class MidiRoutesView : public Gtk::Box
{
public:
    MidiRoutesView (const std::vector<thMidiRoute> &routes,
                    std::function<void (const std::vector<thMidiRoute> &)>
                        changed);

    /* One showing `prefs''s routes: an edit sets them there, which puts
       them live, and saves the file; a Load shows what it read. */
    static MidiRoutesView *forPrefs (gthPrefs *prefs);

    std::vector<thMidiRoute> routes (void) const;

    /* The table shown as `routes' says, where it says otherwise. */
    void setRoutes (const std::vector<thMidiRoute> &routes);

    /* A route at the end, MIDI channel 1 onto engine channel 1, as Add
       route makes; and route `row' taken out, as its Remove does. */
    void addRoute (void);
    void removeRoute (size_t row);

    /* Row `row''s controls, for a harness to set as a person would. */
    Gtk::SpinButton *midiSpin (size_t row);
    Gtk::SpinButton *engineSpin (size_t row);
    Gtk::SpinButton *lowSpin (size_t row);
    Gtk::SpinButton *highSpin (size_t row);

private:
    struct Row
    {
        Gtk::Box        *box;
        Gtk::SpinButton *midi, *engine, *low, *high;
        Gtk::Button     *remove;
    };

    void appendRoute (const thMidiRoute &r);
    void edited (void);
    void renumber (void);

    std::function<void (const std::vector<thMidiRoute> &)> changed_;
    Gtk::Box rows_{Gtk::Orientation::VERTICAL, 4};
    Gtk::Label empty_;
    Gtk::Button add_{"Add route"};
    std::vector<Row> list_;
    bool filling_ = false;
};

#endif /* MIDI_ROUTES_VIEW_H */
