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

#include <memory>

#include "think.h"
#include "thcGenFile.h"
#include "gthPrefs.h"
#include "Access.h"
#include "MidiRoutesView.h"

namespace {

/* How long after the last edit the routes are written to the file. */
const unsigned SAVE_AFTER_MS = 500;

/* A key spinner says the note's name, and takes a name or a number. */
void showsNotes (Gtk::SpinButton *spin)
{
    spin->signal_output().connect([spin]
    {
        spin->set_text(thcGenLoader::noteName((int)spin->get_value()));
        return true;
    }, false);
    spin->signal_input().connect([spin](double &value)
    {
        std::vector<int> notes;
        std::string bad;
        const std::string text = spin->get_text();
        char *end = NULL;
        const long n = strtol(text.c_str(), &end, 10);

        if (!text.empty() && *end == '\0')
            value = (double)n;
        else if (thcGenLoader::parseNoteList(text, notes, bad) &&
                 notes.size() == 1)
            value = notes[0];
        else
            return GTK_INPUT_ERROR;

        return 1;
    }, false);
}

/* The engine spinner's 0 is `off'. */
void showsOff (Gtk::SpinButton *spin)
{
    spin->signal_output().connect([spin]
    {
        const int v = (int)spin->get_value();

        spin->set_text(v == 0 ? "off" : std::to_string(v));
        return true;
    }, false);
    spin->signal_input().connect([spin](double &value)
    {
        const std::string text = spin->get_text();
        char *end = NULL;
        const long n = strtol(text.c_str(), &end, 10);

        if (text == "off")
            value = 0;
        else if (!text.empty() && *end == '\0')
            value = (double)n;
        else
            return GTK_INPUT_ERROR;

        return 1;
    }, false);
}

}

MidiRoutesView::MidiRoutesView (
        const std::vector<thMidiRoute> &routes,
        std::function<void (const std::vector<thMidiRoute> &)> changed)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 4), changed_(changed)
{
    Gtk::Label *about = manage(new Gtk::Label(
        "MIDI channel, the channel it plays (or off), and its keys. A route "
        "here outranks a piece's for its MIDI channel."));

    about->set_wrap(true);
    about->set_xalign(0);
    empty_.set_text("No routes: each MIDI channel plays its own number "
                    "unless the piece says otherwise.");
    empty_.set_wrap(true);
    empty_.set_xalign(0);
    empty_.add_css_class("dim-label");
    append(*about);
    append(rows_);
    append(empty_);
    add_.set_halign(Gtk::Align::START);
    add_.signal_clicked().connect([this]
    {
        addRoute();
    });
    append(add_);

    setRoutes(routes);
}

MidiRoutesView *
MidiRoutesView::forPrefs (gthPrefs *prefs)
{
    /* Live at once; written to the file once the edits stop for a
       moment, rather than on every step of a spinner held down. */
    auto saving = std::make_shared<sigc::connection>();
    MidiRoutesView *view = Gtk::manage(new MidiRoutesView(
        prefs->midiRoutes(),
        [prefs, saving](const std::vector<thMidiRoute> &routes)
        {
            prefs->setMidiRoutes(routes);
            saving->disconnect();
            *saving = Glib::signal_timeout().connect([prefs]
            {
                prefs->Save();
                return false;
            }, SAVE_AFTER_MS);
        }));

    prefs->signal_midi_routes().connect(
        sigc::mem_fun(*view, &MidiRoutesView::setRoutes));
    return view;
}

void
MidiRoutesView::setRoutes (const std::vector<thMidiRoute> &routes)
{
    const std::vector<thMidiRoute> have = this->routes();
    bool same = have.size() == routes.size();

    for (size_t i = 0; same && i < routes.size(); i++)
        same = have[i].midi == routes[i].midi &&
               have[i].engine == routes[i].engine &&
               have[i].lo == routes[i].lo && have[i].hi == routes[i].hi;

    if (same)
        return;

    for (const Row &row : list_)
        rows_.remove(*row.box);
    list_.clear();

    filling_ = true;
    for (const thMidiRoute &r : routes)
        appendRoute(r);
    filling_ = false;
    renumber();
}

void
MidiRoutesView::appendRoute (const thMidiRoute &r)
{
    Row row;

    row.box = manage(new Gtk::Box(Gtk::Orientation::VERTICAL, 2));
    row.box->set_margin_bottom(6);
    row.midi = manage(new Gtk::SpinButton(
        Gtk::Adjustment::create(r.midi + 1, 1, TH_MIDI_CHANNELS, 1)));
    row.engine = manage(new Gtk::SpinButton(
        Gtk::Adjustment::create(r.engine + 1, 0, TH_ENGINE_CHANNELS, 1)));
    row.low = manage(new Gtk::SpinButton(
        Gtk::Adjustment::create(r.lo, 0, 127, 1)));
    row.high = manage(new Gtk::SpinButton(
        Gtk::Adjustment::create(r.hi, 0, 127, 1)));

    Gtk::Button *remove = manage(new Gtk::Button("Remove"));

    showsOff(row.engine);
    showsNotes(row.low);
    showsNotes(row.high);

    for (Gtk::SpinButton *spin : { row.midi, row.engine, row.low, row.high })
    {
        spin->set_numeric(false);
        spin->set_width_chars(4);
        spin->signal_value_changed().connect([this] { edited(); });
    }

    remove->signal_clicked().connect([this, box = row.box]
    {
        for (size_t i = 0; i < list_.size(); i++)
            if (list_[i].box == box)
            {
                removeRoute(i);
                return;
            }
    });

    /* Two lines, so a row fits the width of a pane: where from and to,
       then which keys. */
    Gtk::Box *where = manage(new Gtk::Box(Gtk::Orientation::HORIZONTAL, 4));
    Gtk::Box *keys = manage(new Gtk::Box(Gtk::Orientation::HORIZONTAL, 4));

    where->append(*manage(new Gtk::Label("MIDI")));
    where->append(*row.midi);
    where->append(*manage(new Gtk::Label("plays")));
    where->append(*row.engine);
    where->append(*remove);
    keys->append(*manage(new Gtk::Label("keys")));
    keys->append(*row.low);
    keys->append(*manage(new Gtk::Label("to")));
    keys->append(*row.high);
    row.box->append(*where);
    row.box->append(*keys);
    row.remove = remove;
    rows_.append(*row.box);
    list_.push_back(row);
}

/* Each row's controls named for what they are and which route: a list of
   bare spinners reads as numbers to a screen reader. */
void
MidiRoutesView::renumber (void)
{
    for (size_t i = 0; i < list_.size(); i++)
    {
        const std::string n = " of route " + std::to_string(i + 1);

        nameAs(*list_[i].midi, "MIDI channel" + n);
        nameAs(*list_[i].engine, "Channel it plays" + n);
        nameAs(*list_[i].low, "Lowest key" + n);
        nameAs(*list_[i].high, "Highest key" + n);
        nameAs(*list_[i].remove, "Remove route " + std::to_string(i + 1));
    }

    empty_.set_visible(list_.empty());
}

std::vector<thMidiRoute>
MidiRoutesView::routes (void) const
{
    std::vector<thMidiRoute> out;

    for (const Row &row : list_)
    {
        const int lo = (int)row.low->get_value();
        const int hi = (int)row.high->get_value();

        out.push_back({ (int)row.midi->get_value() - 1,
                        (int)row.engine->get_value() - 1,
                        std::min(lo, hi), std::max(lo, hi) });
    }

    return out;
}

void
MidiRoutesView::edited (void)
{
    if (!filling_ && changed_)
        changed_(routes());
}

void
MidiRoutesView::addRoute (void)
{
    appendRoute({ 0, 0, 0, 127 });
    renumber();
    edited();
}

void
MidiRoutesView::removeRoute (size_t row)
{
    if (row >= list_.size())
        return;

    rows_.remove(*list_[row].box);
    list_.erase(list_.begin() + row);
    renumber();
    edited();
}

Gtk::SpinButton *
MidiRoutesView::midiSpin (size_t row)
{
    return row < list_.size() ? list_[row].midi : NULL;
}

Gtk::SpinButton *
MidiRoutesView::engineSpin (size_t row)
{
    return row < list_.size() ? list_[row].engine : NULL;
}

Gtk::SpinButton *
MidiRoutesView::lowSpin (size_t row)
{
    return row < list_.size() ? list_[row].low : NULL;
}

Gtk::SpinButton *
MidiRoutesView::highSpin (size_t row)
{
    return row < list_.size() ? list_[row].high : NULL;
}
