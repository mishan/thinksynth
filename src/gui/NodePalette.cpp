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

NodePalette::NodePalette (void)
    : pm_(NULL), addBtn_("Add to graph")
{
    set_size_request(210, -1);

    filter_.set_placeholder_text("Filter");
    filter_.signal_changed().connect(
        sigc::mem_fun(*this, &NodePalette::onFilterChanged));

    store_ = Gtk::TreeStore::create(cols_);

    tree_.set_model(store_);
    tree_.append_column("Node", cols_.label);
    tree_.set_headers_visible(false);
    tree_.set_enable_search(false);     /* the filter box does this better */

    tree_.get_selection()->signal_changed().connect(
        sigc::mem_fun(*this, &NodePalette::onSelectionChanged));

    /* Double-click adds, because that is what a palette does. */
    tree_.signal_row_activated().connect(
        sigc::mem_fun(*this, &NodePalette::onRowActivated));

    scroller_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    scroller_.add(tree_);

    detail_.set_alignment(Gtk::ALIGN_START, Gtk::ALIGN_START);
    detail_.set_padding(6, 4);
    detail_.set_line_wrap(true);
    detail_.set_size_request(200, -1);

    addBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &NodePalette::onAddClicked));
    addBtn_.set_sensitive(false);

    pack_start(filter_, Gtk::PACK_SHRINK);
    pack_start(scroller_);
    pack_start(detail_, Gtk::PACK_SHRINK);
    pack_start(addBtn_, Gtk::PACK_SHRINK);
}

int NodePalette::populate (const string &pluginPath, thPluginManager *pm)
{
    pm_ = pm;

    const int n = catalog_.scan(pluginPath);

    rebuild();

    if (n == 0)
        detail_.set_markup("<i>No plugins found.</i>");

    return n;
}

void NodePalette::rebuild (void)
{
    const Glib::ustring needle = filter_.get_text().lowercase();

    store_->clear();

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

    m_signal_add_(s.raw());
}

void NodePalette::onAddClicked (void)
{
    const string spelling = selectedSpelling();

    if (!spelling.empty())
        m_signal_add_(spelling);
}

void NodePalette::setSensitive (bool s)
{
    filter_.set_sensitive(s);
    tree_.set_sensitive(s);
    addBtn_.set_sensitive(s && !selectedSpelling().empty());
}
