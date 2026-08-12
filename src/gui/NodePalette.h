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

#ifndef NODE_PALETTE_H
#define NODE_PALETTE_H 1

#include "../NodeCatalog.h"

class thPluginManager;

/*
 * The list of things that can be added to a graph, by category.
 *
 * A tree of 11 categories over 62 plugins, with a filter box -- a flat list of
 * 62 is too many to scan and the categories are how the .dsp names them
 * anyway ("osc::simple"), so the grouping is the format's, not an invention.
 *
 * Selecting a plugin loads it to show what it does and what ports it has.
 * That is a dlopen per selection rather than 62 at startup, which is the only
 * reason the palette opens instantly.
 */
class NodePalette : public Gtk::Box
{
public:
    NodePalette (void);

    /* Scans for plugins. `pm' is used lazily, to describe a selection. */
    int populate (const string &pluginPath, thPluginManager *pm);

    /* Emitted when a plugin is chosen to be added: "osc::simple". */
    typedef sigc::signal<void(string)> type_signal_add;
    type_signal_add signal_add (void) { return m_signal_add_; }

    /* Emitted when the Control entry is chosen. A control is not a plugin --
       it is a `@name' block -- so it needs its own signal and its own row at
       the top of the tree rather than a pretend category. */
    typedef sigc::signal<void()> type_signal_add_control;
    type_signal_add_control signal_add_control (void) {
        return m_signal_add_control_;
    }

    void setSensitive (bool s);

protected:
    void onSelectionChanged (void);
    void onRowActivated (const Gtk::TreeModel::Path &path,
                         Gtk::TreeViewColumn *col);
    void onAddClicked (void);
    void onFilterChanged (void);

    /* The spelling under the cursor, or "" if a category row is selected. */
    string selectedSpelling (void);

    void rebuild (void);

private:
    struct Columns : public Gtk::TreeModel::ColumnRecord {
        Gtk::TreeModelColumn<Glib::ustring> label;
        Gtk::TreeModelColumn<Glib::ustring> spelling;   /* "" for a category */

        Columns (void) { add(label); add(spelling); }
    };

    Columns cols_;

    NodeCatalog catalog_;
    thPluginManager *pm_;

    Gtk::Entry filter_;
    Gtk::ScrolledWindow scroller_;
    Gtk::TreeView tree_;
    Glib::RefPtr<Gtk::TreeStore> store_;
    Gtk::Label detail_;
    Gtk::Button addBtn_;

    type_signal_add m_signal_add_;
    type_signal_add_control m_signal_add_control_;
};

#endif /* NODE_PALETTE_H */
