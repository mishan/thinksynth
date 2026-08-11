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

#ifndef GUI_DIALOGS_H
#define GUI_DIALOGS_H

/*
 * What is left of Gtk::Dialog::run().
 *
 * GTK4 removed it, and the reason is sound: run() spins a nested main loop,
 * which means a dialog can be answered while the code that opened it is still
 * on the stack, and everything that code was in the middle of is still half
 * done. Every dialog is asynchronous now -- show it, and hear about the answer
 * later.
 *
 * Most of this tree's dialogs never wanted the answer. They say that something
 * failed, and there is one button, and nothing follows. Those become
 * showError(), which shows a message and forgets it.
 *
 * The ones that do want an answer -- the file choosers, and the node editor's
 * new-control form -- cannot be helped by anything general; each has to be
 * split at the point it used to block, with the rest of the work moved into a
 * response handler. They are written out where they are used.
 */

/* Shows `text' as an error, over `parent' if there is one, and takes care of
   the dialog's own lifetime. Returns immediately. */
void showError (Gtk::Window *parent, const Glib::ustring &text,
                const Glib::ustring &secondary = Glib::ustring());

/* The same, at warning severity. */
void showWarning (Gtk::Window *parent, const Glib::ustring &text,
                  const Glib::ustring &secondary = Glib::ustring());

/* Hides a dialog and frees it once the response that asked for it has
   finished being delivered.
 *
 * The delay matters: deleting a widget from inside its own signal handler
 * tears down the object the emission is still walking. This is the same
 * reason MainSynthWindow defers a patch save out of the click that asked for
 * it. */
void closeDialog (Gtk::Dialog *dlg);

#endif /* GUI_DIALOGS_H */
