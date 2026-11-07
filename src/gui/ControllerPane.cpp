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

static const char *HEADINGS[7] = { "Layer", "Control", "Does", "Where", "Now",
                                   "", "" };

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
    status_.set_selectable(true);

    grid_.set_column_spacing(12);
    grid_.set_row_spacing(2);

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

void ControllerPane::rebuild (size_t rows)
{
    while (Gtk::Widget *w = grid_.get_first_child())
        grid_.remove(*w);
    cells_.clear();

    for (int c = 0; c < 7; c++)
    {
        Gtk::Label *h = Gtk::make_managed<Gtk::Label>(HEADINGS[c]);

        h->set_xalign(0);
        h->add_css_class("heading");
        grid_.attach(*h, c, 0);
    }

    for (size_t r = 0; r < rows; r++)
    {
        std::array<Gtk::Label *, 7> row;

        for (int c = 0; c < 7; c++)
        {
            row[c] = Gtk::make_managed<Gtk::Label>();
            row[c]->set_xalign(0);
            grid_.attach(*row[c], c, (int)r + 1);
        }
        row[5]->add_css_class("dim-label");
        row[6]->add_css_class("error");
        cells_.push_back(row);
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

    const thhState now = source_->state();
    const std::vector<thhPaneRow> rows = thhPaneRows(
        source_->profile(), thhIndexControls(), now, source_->channel(),
        [this](int ch, const std::string &arg) {
            return source_->hasArg(ch, arg);
        });

    if (source_->generation() != generation_ || rows.size() != cells_.size())
    {
        generation_ = source_->generation();
        rebuild(rows.size());
    }

    for (size_t r = 0; r < rows.size(); r++)
    {
        const thhPaneRow &row = rows[r];
        const std::string yours = row.yours ? "yours" : "";
        const std::string *text[7] = { &row.layer, &row.control, &row.action,
                                       &row.target, &row.value, &yours,
                                       &row.warning };

        for (int c = 0; c < 7; c++)
            if (cells_[r][c]->get_text().raw() != *text[c])
                cells_[r][c]->set_text(*text[c]);
    }

    const std::string why = source_->status();
    std::string line = !problem_.empty() ? problem_
        : now.connected ? "Valve Index controller connected."
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
