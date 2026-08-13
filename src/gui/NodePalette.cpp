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

    store_ = Gio::ListStore<PaletteRow>::create();

    /* Each row is drawn as an expander wrapping a label. The expander is what
       draws the arrow and the indent, and it needs the Gtk::TreeListRow --
       not our PaletteRow -- because depth and expanded state live there. */
    Glib::RefPtr<Gtk::SignalListItemFactory> factory =
        Gtk::SignalListItemFactory::create();

    factory->signal_setup().connect(
        [](const Glib::RefPtr<Gtk::ListItem> &item)
        {
            Gtk::TreeExpander *expander =
                Gtk::make_managed<Gtk::TreeExpander>();
            Gtk::Label *label = Gtk::make_managed<Gtk::Label>();

            label->set_halign(Gtk::Align::START);
            label->set_ellipsize(Pango::EllipsizeMode::END);

            expander->set_child(*label);
            item->set_child(*expander);
        });

    factory->signal_bind().connect(
        [](const Glib::RefPtr<Gtk::ListItem> &item)
        {
            Gtk::TreeExpander *expander =
                dynamic_cast<Gtk::TreeExpander *>(item->get_child());

            if (expander == NULL)
                return;

            Glib::RefPtr<Gtk::TreeListRow> treeRow =
                std::dynamic_pointer_cast<Gtk::TreeListRow>(item->get_item());

            expander->set_list_row(treeRow);

            Gtk::Label *label =
                dynamic_cast<Gtk::Label *>(expander->get_child());

            Glib::RefPtr<PaletteRow> row =
                treeRow ? std::dynamic_pointer_cast<PaletteRow>(
                              treeRow->get_item())
                        : Glib::RefPtr<PaletteRow>();

            if (label)
                label->set_text(row ? row->label() : Glib::ustring());
        });

    tree_.set_factory(factory);

    /* Double-click, or Enter, adds -- because that is what a palette does. */
    tree_.signal_activate().connect(
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

    store_->remove_all();

    /* The control goes first and on its own, above the plugin categories.
       It is the one thing here that is not a plugin: `@blim' is a block in
       the file, not something in plugins/. Filing it under a made-up
       category would say otherwise. */
    {
        const Glib::ustring label = "Control (slider)";

        if (needle.empty() ||
            label.lowercase().find(needle) != Glib::ustring::npos ||
            Glib::ustring("control").find(needle) != Glib::ustring::npos)
            store_->append(PaletteRow::create(label, CONTROL_SPELLING));
    }

    for (size_t c = 0; c < catalog_.categories().size(); c++)
    {
        const string &cat = catalog_.categories()[c];
        const vector<NodeCatalog::Entry> &list = catalog_.inCategory(cat);

        Glib::RefPtr<PaletteRow> catRow;

        for (size_t e = 0; e < list.size(); e++)
        {
            if (!needle.empty())
            {
                const Glib::ustring hay =
                    Glib::ustring(list[e].spelling).lowercase();

                if (hay.find(needle) == Glib::ustring::npos)
                    continue;
            }

            if (!catRow)
                catRow = PaletteRow::create(cat, "");

            catRow->addChild(PaletteRow::create(list[e].name,
                                                list[e].spelling));
        }

        /* Appended once it has its children, the way the patch selector fills
           a row before the model sees it: a category handed over empty is a
           category Gtk::TreeListModel is told has nothing to expand. */
        if (catRow)
            store_->append(catRow);
    }

    /* A fresh model each time, so expanded state starts from nothing rather
       than from whatever the last filter left behind.

       autoexpand while filtering, because filtering with everything collapsed
       hides the results -- which is what expand_all() was for. */
    treeModel_ = Gtk::TreeListModel::create(
        store_,
        [](const Glib::RefPtr<Glib::ObjectBase> &item)
            -> Glib::RefPtr<Gio::ListModel>
        {
            Glib::RefPtr<PaletteRow> row =
                std::dynamic_pointer_cast<PaletteRow>(item);

            /* Null means "a leaf": no arrow, nothing to expand. */
            if (!row || !row->children())
                return Glib::RefPtr<Gio::ListModel>();

            return row->children();
        },
        false,                              /* passthrough */
        !filter_.get_text().empty());       /* autoexpand */

    selection_ = Gtk::SingleSelection::create(treeModel_);

    /* Nothing chosen until the user chooses; otherwise selecting row 0 on
       every rebuild would dlopen a plugin nobody asked about. */
    selection_->set_autoselect(false);
    selection_->set_can_unselect(true);
    selection_->set_selected(GTK_INVALID_LIST_POSITION);

    selection_->property_selected().signal_changed().connect(
        sigc::mem_fun(*this, &NodePalette::onSelectionChanged));

    tree_.set_model(selection_);
}

void NodePalette::onFilterChanged (void)
{
    rebuild();
}

/* The PaletteRow at `position', unwrapped from the Gtk::TreeListRow the view
   actually holds. Null for a position that is not there. */
Glib::RefPtr<PaletteRow> NodePalette::rowAt (guint position) const
{
    if (!treeModel_)
        return Glib::RefPtr<PaletteRow>();

    Glib::RefPtr<Gtk::TreeListRow> treeRow = treeModel_->get_row(position);

    if (!treeRow)
        return Glib::RefPtr<PaletteRow>();

    return std::dynamic_pointer_cast<PaletteRow>(treeRow->get_item());
}

string NodePalette::selectedSpelling (void)
{
    if (!selection_)
        return "";

    Glib::RefPtr<Gtk::TreeListRow> treeRow =
        std::dynamic_pointer_cast<Gtk::TreeListRow>(
            selection_->get_selected_item());

    if (!treeRow)
        return "";

    Glib::RefPtr<PaletteRow> row =
        std::dynamic_pointer_cast<PaletteRow>(treeRow->get_item());

    return row ? row->spelling() : "";
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

void NodePalette::onRowActivated (guint position)
{
    if (!treeModel_)
        return;

    Glib::RefPtr<Gtk::TreeListRow> treeRow = treeModel_->get_row(position);

    if (!treeRow)
        return;

    Glib::RefPtr<PaletteRow> row =
        std::dynamic_pointer_cast<PaletteRow>(treeRow->get_item());

    if (!row)
        return;

    /* A category row toggles rather than adding anything. Expanded state
       belongs to the Gtk::TreeListRow, not to ours. */
    if (row->isCategory())
    {
        treeRow->set_expanded(!treeRow->get_expanded());
        return;
    }

    if (row->spelling() == CONTROL_SPELLING)
        m_signal_add_control_();
    else
        m_signal_add_(row->spelling());
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
