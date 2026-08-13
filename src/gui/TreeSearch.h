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

#ifndef GTH_TREESEARCH_H
#define GTH_TREESEARCH_H

#include <gtkmm.h>

/* Turn off GtkTreeView's interactive search.
 *
 * TEMPORARY. NodePalette is the last GtkTreeView in the tree; PatchSelWindow
 * and MidiMap are Gtk::ColumnViews now, which have no interactive search to
 * turn off. This header and its one call site go when the palette follows --
 * a workaround for a deprecated widget is not worth a test or a long life.
 *
 * Typing a printable key into a GtkTreeView makes it build a search popover on
 * demand, and in GTK4 that popover is parented in a way GTK's own CSS machinery
 * rejects:
 *
 *   Gtk-CRITICAL: gtk_css_node_insert_after: assertion
 *   'previous_sibling == NULL || previous_sibling->parent == parent' failed
 *
 * from gtk_tree_view_ensure_interactive_directory, by way of
 * gtk_widget_set_parent. GtkTreeView is deprecated in GTK4 and its interactive
 * search has not survived the move; nothing on our side is malformed, and there
 * is nothing we can do at the call site to make the popover legal.
 *
 * Both switches, because they close different doors. `enable_search' gates the
 * printable-key path, which is how a user hits this. `search_column' gates the
 * popover's construction outright, which also covers the Ctrl+F keybinding --
 * that one runs the class handler directly and does not consult
 * `enable_search', so turning only the first one off leaves the accelerator
 * live.
 *
 * No loss worth naming: the longest of these lists is sixteen rows. NodePalette
 * had already done this by hand for its own reason -- a filter entry does the
 * job better -- which is where the pattern comes from.
 */
inline void gthDisableTreeSearch (Gtk::TreeView &view)
{
    view.set_enable_search(false);
    view.set_search_column(-1);
}

#endif /* GTH_TREESEARCH_H */
