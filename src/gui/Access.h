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

#ifndef GTH_ACCESS_H
#define GTH_ACCESS_H

#include <gtkmm.h>

/* `w' is called `name' by a screen reader, for a control whose own text
 * is a sign ("+") or that has none. A label beside a control names it by
 * add_mnemonic_label instead.
 *
 * A button is labelled by its own label, and labelled-by outranks a name,
 * so that relation goes: without it a "+" button stayed "+".
 *
 * In C because gtkmm's update_property takes a Glib::ValueBase, which is
 * four lines for one string. */
inline void
nameAs (Gtk::Widget &w, const Glib::ustring &name)
{
    gtk_accessible_reset_relation(GTK_ACCESSIBLE(w.gobj()),
                                  GTK_ACCESSIBLE_RELATION_LABELLED_BY);
    gtk_accessible_update_property(GTK_ACCESSIBLE(w.gobj()),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   name.c_str(), -1);
}

#endif /* GTH_ACCESS_H */
