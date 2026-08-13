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

#ifndef GTH_COLUMNUTIL_H
#define GTH_COLUMNUTIL_H

#include <gtkmm.h>

/* One text column of a Gtk::ColumnView.
 *
 * A ColumnView column is a title plus a factory, and the factory is two
 * callbacks: `setup' builds a widget for a recycled row, `bind' fills it in
 * for the item that row is currently showing. Written out per column that is
 * eight lines of the same eight lines, which is how a ListStore's four-line
 * append turns into a screenful.
 *
 * So: pass a function that turns a row object into the text for this column.
 * The widget, the recycling and the casting stay here.
 *
 * `text' is handed the item as a plain Glib::ObjectBase; the caller knows what
 * it really is and downcasts. That keeps this out of templates, which for four
 * columns in two windows is not a trade worth making.
 */
typedef sigc::slot<Glib::ustring(const Glib::RefPtr<Glib::ObjectBase> &)>
        GthColumnText;

Glib::RefPtr<Gtk::ColumnViewColumn>
gthTextColumn (const Glib::ustring &title, const GthColumnText &text,
               Gtk::Align align = Gtk::Align::START);

#endif /* GTH_COLUMNUTIL_H */
