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

#include "ColumnUtil.h"

Glib::RefPtr<Gtk::ColumnViewColumn>
gthTextColumn (const Glib::ustring &title, const GthColumnText &text,
               Gtk::Align align)
{
    Glib::RefPtr<Gtk::SignalListItemFactory> factory =
        Gtk::SignalListItemFactory::create();

    /* One label per row widget, made once and reused. ColumnView recycles the
       widgets as it scrolls, which is the whole reason setup and bind are
       separate: setup runs per widget, bind runs per widget per item. */
    factory->signal_setup().connect(
        [align](const Glib::RefPtr<Gtk::ListItem> &item)
        {
            Gtk::Label *label = Gtk::make_managed<Gtk::Label>();

            label->set_halign(align);

            /* Long patch names and long parameter names both turn up here,
               and a column that grows to fit the worst one pushes everything
               after it off the window. */
            label->set_ellipsize(Pango::EllipsizeMode::END);

            item->set_child(*label);
        });

    factory->signal_bind().connect(
        [text](const Glib::RefPtr<Gtk::ListItem> &item)
        {
            Gtk::Label *label = dynamic_cast<Gtk::Label *>(item->get_child());

            if (label == NULL)
                return;

            const Glib::RefPtr<Glib::ObjectBase> row = item->get_item();

            /* A bind with nothing to bind to happens while a model is being
               replaced. Empty rather than a crash or a stale value. */
            label->set_text(row ? text(row) : Glib::ustring());
        });

    Glib::RefPtr<Gtk::ColumnViewColumn> column =
        Gtk::ColumnViewColumn::create(title, factory);

    return column;
}
