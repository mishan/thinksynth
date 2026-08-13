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

#include <stdio.h>

#include <gtkmm.h>

#include "think.h"

#include "../NodeCatalog.h"
#include "NodePalette.h"
#include "TreeSearch.h"

/* Stands in the spelling column where a plugin would put "osc::simple". Not
   a legal plugin name, so it cannot collide with one. */
#define CONTROL_SPELLING  "@control"

NodePalette::NodePalette (void)
    : Gtk::Box(Gtk::Orientation::VERTICAL),
      pm_(NULL), addBtn_("Add to graph")
{
    set_size_request(210, -1);

    filter_.set_placeholder_text("Filter");
    filter_.signal_changed().connect(
        sigc::mem_fun(*this, &NodePalette::onFilterChanged));

    store_ = Gtk::TreeStore::create(cols_);

    tree_.set_model(store_);
    tree_.append_column("Node", cols_.label);
    tree_.set_headers_visible(false);
    /* The filter box does this better, and GTK's own popover is broken --
       see TreeSearch.h. */
    gthDisableTreeSearch(tree_);

    tree_.get_selection()->signal_changed().connect(
        sigc::mem_fun(*this, &NodePalette::onSelectionChanged));

    /* Double-click adds, because that is what a palette does. */
    tree_.signal_row_activated().connect(
        sigc::mem_fun(*this, &NodePalette::onRowActivated));

    scroller_.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    scroller_.set_child(tree_);

    detail_.set_xalign(0.0);
    detail_.set_yalign(0.0);
    detail_.set_margin_start(6);
    detail_.set_margin_end(6);
    detail_.set_margin_top(4);
    detail_.set_margin_bottom(4);
    detail_.set_wrap(true);
    detail_.set_size_request(200, -1);

    addBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &NodePalette::onAddClicked));
    addBtn_.set_sensitive(false);

    append(filter_);
    scroller_.set_vexpand(true);
    append(scroller_);
    append(detail_);
    append(addBtn_);
}

int NodePalette::populate (const string &pluginPath, thPluginManager *pm)
{
    pm_ = pm;

    const int n = catalog_.scan(pluginPath);

    rebuild();

    if (n == 0)
    {
        /* Naming the directory is the whole message: "no plugins found" sends
           you looking at the palette, and the answer is always the path. */
        detail_.set_markup("<i>No plugins in</i>\n<tt>" +
                           Glib::Markup::escape_text(pluginPath) + "</tt>\n"
                           "<small>Build them, or set "
                           "THINK_PLUGIN_PATH.</small>");
    }

    return n;
}

void NodePalette::rebuild (void)
{
    const Glib::ustring needle = filter_.get_text().lowercase();

    store_->clear();

    /* The control goes first and on its own, above the plugin categories.
       It is the one thing here that is not a plugin: `@blim' is a block in
       the file, not something in plugins/. Filing it under a made-up
       category would say otherwise. */
    {
        const Glib::ustring label = "Control (slider)";

        if (needle.empty() ||
            label.lowercase().find(needle) != Glib::ustring::npos ||
            Glib::ustring("control").find(needle) != Glib::ustring::npos)
        {
            Gtk::TreeModel::Row row = *(store_->append());

            row[cols_.label] = label;
            row[cols_.spelling] = CONTROL_SPELLING;
        }
    }

    for (size_t c = 0; c < catalog_.categories().size(); c++)
    {
        const string &cat = catalog_.categories()[c];
        const vector<NodeCatalog::Entry> &list = catalog_.inCategory(cat);

        Gtk::TreeModel::Row catRow;
        bool haveCat = false;

        for (size_t e = 0; e < list.size(); e++)
        {
            if (!needle.empty())
            {
                const Glib::ustring hay =
                    Glib::ustring(list[e].spelling).lowercase();

                if (hay.find(needle) == Glib::ustring::npos)
                    continue;
            }

            if (!haveCat)
            {
                catRow = *(store_->append());
                catRow[cols_.label] = cat;
                catRow[cols_.spelling] = "";
                haveCat = true;
            }

            Gtk::TreeModel::Row row = *(store_->append(catRow.children()));

            row[cols_.label] = list[e].name;
            row[cols_.spelling] = list[e].spelling;
        }
    }

    /* Filtering with everything collapsed hides the results. */
    if (!filter_.get_text().empty())
        tree_.expand_all();
}

void NodePalette::onFilterChanged (void)
{
    rebuild();
}

string NodePalette::selectedSpelling (void)
{
    Gtk::TreeModel::iterator i = tree_.get_selection()->get_selected();

    if (!i)
        return "";

    Glib::ustring s = (*i)[cols_.spelling];

    return s.raw();
}

void NodePalette::onSelectionChanged (void)
{
    const string spelling = selectedSpelling();

    addBtn_.set_sensitive(!spelling.empty());

    if (spelling.empty())
    {
        detail_.set_text("");
        return;
    }

    if (spelling == CONTROL_SPELLING)
    {
        detail_.set_markup("<b>Control</b>\n"
                           "A slider wired into as many parameters as you "
                           "like.\n<small>out: its value</small>");
        return;
    }

    NodeCatalog::Entry info;

    if (!catalog_.describe(spelling, pm_, info))
    {
        /* Said rather than hidden: a plugin that will not load is one the
           palette must not pretend it can add. */
        detail_.set_markup("<b>" + Glib::Markup::escape_text(spelling) +
                           "</b>\n<i>will not load</i>");
        addBtn_.set_sensitive(false);
        return;
    }

    string ins, outs;

    for (size_t k = 0; k < info.ports.size(); k++)
    {
        string &side = info.ports[k].isInput ? ins : outs;

        if (!side.empty())
            side += ", ";

        side += info.ports[k].name;
    }

    string text = "<b>" + Glib::Markup::escape_text(spelling) + "</b>";

    if (!info.desc.empty())
        text += "\n" + Glib::Markup::escape_text(info.desc);

    if (!ins.empty())
        text += "\n<small>in: " + Glib::Markup::escape_text(ins) + "</small>";

    if (!outs.empty())
        text += "\n<small>out: " + Glib::Markup::escape_text(outs) + "</small>";

    if (ins.empty() && outs.empty())
        text += "\n<small><i>declares no ports</i></small>";

    detail_.set_markup(text);
}

void NodePalette::onRowActivated (const Gtk::TreeModel::Path &path,
                                  Gtk::TreeViewColumn *col)
{
    (void)col;

    Gtk::TreeModel::iterator i = store_->get_iter(path);

    if (!i)
        return;

    Glib::ustring s = (*i)[cols_.spelling];

    /* A category row toggles rather than adding anything. */
    if (s.empty())
    {
        if (tree_.row_expanded(path))
            tree_.collapse_row(path);
        else
            tree_.expand_row(path, false);

        return;
    }

    if (s.raw() == CONTROL_SPELLING)
        m_signal_add_control_();
    else
        m_signal_add_(s.raw());
}

void NodePalette::onAddClicked (void)
{
    const string spelling = selectedSpelling();

    if (spelling == CONTROL_SPELLING)
        m_signal_add_control_();
    else if (!spelling.empty())
        m_signal_add_(spelling);
}

void NodePalette::setSensitive (bool s)
{
    filter_.set_sensitive(s);
    tree_.set_sensitive(s);
    addBtn_.set_sensitive(s && !selectedSpelling().empty());
}
