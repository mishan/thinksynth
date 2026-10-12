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

/* What a screen reader calls a control, for the controls GTK cannot name
 * by itself: a label beside one names nothing until it is said to, and an
 * icon or a sign on a button is read out as the sign.
 *
 * In C because gtkmm's update_relation takes a Glib::ValueBase, and a
 * relation's value is a list of accessibles it has no wrapper for. */

/* `w' is named by `label', and follows it if the label's text changes. */
inline void
labelWith (Gtk::Widget &w, Gtk::Widget &label)
{
    gtk_accessible_update_relation(GTK_ACCESSIBLE(w.gobj()),
                                   GTK_ACCESSIBLE_RELATION_LABELLED_BY,
                                   label.gobj(), NULL, -1);
}

/* `w' is called `name'. */
inline void
nameAs (Gtk::Widget &w, const Glib::ustring &name)
{
    gtk_accessible_update_property(GTK_ACCESSIBLE(w.gobj()),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   name.c_str(), -1);
}

#endif /* GTH_ACCESS_H */
