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
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <filesystem>
#include <fstream>

#include "ControllerPane.h"
#include "thhIndex.h"

namespace fs = std::filesystem;

/* What Open file writes where there is no profile yet: changes, not a copy,
   so the controller's own profile still reaches its player as it grows. */
static const char TEMPLATE[] =
    "# Your changes to the Valve Index controller's own profile, which\n"
    "# docs/HID.md lists. A control bound here replaces every binding of it\n"
    "# there, a layer replaces the layer of its name, `ignore NAME;' drops\n"
    "# either, and base, channel and velocity are yours if you set them.\n"
    "device index {\n"
    "};\n";

ControllerPane::ControllerPane (void)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 6), source_(NULL), generation_(0),
      buttons_(Gtk::Orientation::HORIZONTAL, 6), reload_("_Reload", true),
      open_("_Open file", true)
{
    set_margin(6);

    status_.set_xalign(0);
    status_.set_wrap(true);
    status_.set_wrap_mode(Pango::WrapMode::WORD_CHAR);

    grid_.set_column_spacing(12);

    reload_.signal_clicked().connect(
        sigc::mem_fun(*this, &ControllerPane::onReload));
    open_.signal_clicked().connect(
        sigc::mem_fun(*this, &ControllerPane::onOpen));
    buttons_.append(reload_);
    buttons_.append(open_);

    append(status_);
    append(grid_);
    append(buttons_);

    /* Live only while it can be seen. */
    signal_map().connect([this]() {
        tick_ = Glib::signal_timeout().connect(
            sigc::mem_fun(*this, &ControllerPane::onTick), 100);
        onTick();
    });
    signal_unmap().connect([this]() { tick_.disconnect(); });

    onTick();
}

void ControllerPane::setSource (thhPaneSource *source)
{
    source_ = source;
    generation_ = (unsigned)-1;
    problem_.clear();
    onTick();
}

/* A binding a block of its own, one above the other, so the pane fits its
   width and only ever scrolls down. */
void ControllerPane::rebuild (size_t rows)
{
    while (Gtk::Widget *w = grid_.get_first_child())
        grid_.remove(*w);
    cells_.clear();

    for (size_t r = 0; r < rows; r++)
    {
        const int y = (int)r * 3;
        Cells c;

        c.head = Gtk::make_managed<Gtk::Label>();
        c.now = Gtk::make_managed<Gtk::Label>();
        c.does = Gtk::make_managed<Gtk::Label>();
        c.warning = Gtk::make_managed<Gtk::Label>();

        c.head->set_xalign(0);
        c.head->set_hexpand(true);
        c.head->set_margin_top(r ? 6 : 0);
        c.head->add_css_class("heading");
        c.now->set_xalign(1);
        c.now->set_width_chars(5);
        c.now->set_margin_top(r ? 6 : 0);
        for (Gtk::Label *l : { c.does, c.warning })
        {
            l->set_xalign(0);
            l->set_wrap(true);
            l->set_wrap_mode(Pango::WrapMode::WORD_CHAR);
        }
        c.does->add_css_class("dim-label");
        c.warning->add_css_class("error");

        grid_.attach(*c.head, 0, y);
        grid_.attach(*c.now, 1, y);
        grid_.attach(*c.does, 0, y + 1, 2, 1);
        grid_.attach(*c.warning, 0, y + 2, 2, 1);
        cells_.push_back(c);
    }
}

bool ControllerPane::onTick (void)
{
    reload_.set_sensitive(source_ != NULL);
    open_.set_sensitive(source_ != NULL);

    if (!source_)
    {
        status_.set_text("Controllers are read on Linux only.");
        rebuild(0);
        return true;
    }

    /* Each controller's rows, under its hand where there are two. */
    std::vector<thhPaneRow> rows;
    std::vector<std::string> hands;
    int connected = 0;

    for (int i = 0; i < source_->controllers(); i++)
    {
        const thhState now = source_->stateOf(i);
        const std::string hand = source_->handOf(i);

        if (!now.connected)
            continue;

        std::vector<thhPaneRow> mine = thhPaneRows(
            source_->profileOf(i), thhIndexControls(), now, source_->channel(),
            [this](int ch, const std::string &arg) {
                return source_->hasArg(ch, arg);
            });
        connected++;
        if (!hand.empty())
        {
            hands.push_back(hand);
            for (thhPaneRow &r : mine)
                r.layer = hand + (r.layer.empty() ? "" : " " + r.layer);
        }
        rows.insert(rows.end(), mine.begin(), mine.end());
    }

    if (source_->generation() != generation_ || rows.size() != cells_.size())
    {
        generation_ = source_->generation();
        rebuild(rows.size());
    }

    for (size_t r = 0; r < rows.size(); r++)
    {
        const thhPaneRow &row = rows[r];
        const Cells &c = cells_[r];
        const std::string head = (row.layer.empty() ? "" : row.layer + ": ") +
                                 row.control;
        /* The keyboard's channel is where nearly everything plays, so only
           another one is worth a word. */
        const bool keys = row.target.compare(0, 14, "the keyboard's") == 0;
        const std::string does = row.action +
                                 (row.target.empty() || keys ? ""
                                                             : ", on " + row.target) +
                                 (row.yours ? " (yours)" : "");
        const std::pair<Gtk::Label *, const std::string *> text[4] = {
            { c.head, &head }, { c.now, &row.value }, { c.does, &does },
            { c.warning, &row.warning } };

        for (const auto &t : text)
            if (t.first->get_text().raw() != *t.second)
                t.first->set_text(*t.second);
        c.warning->set_visible(!row.warning.empty());
    }

    const std::string why = source_->status();
    std::string line = !problem_.empty() ? problem_
        : hands.size() > 1 ? std::to_string(connected) +
                                 " Valve Index controllers connected, " +
                                 hands[0] + " and " + hands[1] + " playing."
        : connected ? "Valve Index controller connected."
        : why.empty() ? "Looking for a Valve Index controller..."
        : why + ".";

    line += "\nProfile: the controller's own" +
            (fs::exists(source_->path()) ? ", with your changes in " +
                                               source_->path()
                                         : std::string(" (no changes of yours "
                                                       "yet)"));
    if (status_.get_text().raw() != line)
        status_.set_text(line);

    return true;
}

void ControllerPane::onReload (void)
{
    std::string err;

    problem_ = source_ && !source_->reload(err) ? err : "";
    onTick();
}

void ControllerPane::onOpen (void)
{
    const fs::path path = source_->path();
    std::error_code ec;

    problem_.clear();
    if (!fs::exists(path, ec))
    {
        fs::create_directories(path.parent_path(), ec);
        std::ofstream(path) << TEMPLATE;
    }

    try
    {
        Gio::AppInfo::launch_default_for_uri(
            Glib::filename_to_uri(path.string()));
    }
    catch (const Glib::Error &e)
    {
        problem_ = "Can't open " + path.string() + ": " + e.what();
    }
    onTick();
}
