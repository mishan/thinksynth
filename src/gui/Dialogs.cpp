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

#include <filesystem>
#include <system_error>

#include <gtkmm.h>

#include "Dialogs.h"

static void freeDialog (Gtk::Dialog *dlg)
{
    delete dlg;
}

void closeDialog (Gtk::Dialog *dlg)
{
    if (dlg == NULL)
        return;

    dlg->set_visible(false);

    Glib::signal_idle().connect_once(
        sigc::bind(sigc::ptr_fun(&freeDialog), dlg));
}

static void message (Gtk::Window *parent, Gtk::MessageType kind,
                     const Glib::ustring &text, const Glib::ustring &secondary)
{
    /* On the heap and outliving this call, which is the whole difference from
       run(): the dialog is still on screen when we return. */
    Gtk::MessageDialog *dlg = parent != NULL
        ? new Gtk::MessageDialog(*parent, text, false, kind,
                                 Gtk::ButtonsType::OK, true)
        : new Gtk::MessageDialog(text, false, kind, Gtk::ButtonsType::OK, true);

    if (!secondary.empty())
        dlg->set_secondary_text(secondary);

    dlg->set_modal(true);

    /* Whatever the answer was -- there is only one button, and the window
       manager's close counts too -- the dialog has said its piece. */
    dlg->signal_response().connect(
        sigc::hide(sigc::bind(sigc::ptr_fun(&closeDialog),
                              (Gtk::Dialog *)dlg)));

    dlg->present();
}

void showError (Gtk::Window *parent, const Glib::ustring &text,
                const Glib::ustring &secondary)
{
    message(parent, Gtk::MessageType::ERROR, text, secondary);
}

void showWarning (Gtk::Window *parent, const Glib::ustring &text,
                  const Glib::ustring &secondary)
{
    message(parent, Gtk::MessageType::WARNING, text, secondary);
}

std::string chosenPath (Gtk::FileChooser &chooser)
{
    const Glib::RefPtr<Gio::File> f = chooser.get_file();

    return f ? f->get_path() : std::string();
}

/* Yes or no, and the dialog goes either way. */
static void overwriteAnswered (int response, Gtk::MessageDialog *dlg,
                               sigc::slot<void ()> done)
{
    closeDialog(dlg);

    if (response == Gtk::ResponseType::OK)
        done();
}

void confirmOverwrite (Gtk::Window *parent, const std::string &path,
                       const sigc::slot<void ()> &done)
{
    std::error_code ec;

    if (!std::filesystem::exists(path, ec))
    {
        done();
        return;
    }

    const std::string name =
        std::filesystem::path(path).filename().string();

    Gtk::MessageDialog *dlg = parent != NULL
        ? new Gtk::MessageDialog(*parent, "Replace " + name + "?", false,
                                 Gtk::MessageType::QUESTION,
                                 Gtk::ButtonsType::NONE, true)
        : new Gtk::MessageDialog("Replace " + name + "?", false,
                                 Gtk::MessageType::QUESTION,
                                 Gtk::ButtonsType::NONE, true);

    dlg->set_secondary_text("A file at " + path + " already exists. "
                            "Replacing it cannot be undone.");
    dlg->add_button("_Cancel", Gtk::ResponseType::CANCEL);
    dlg->add_button("_Replace", Gtk::ResponseType::OK);
    dlg->set_modal(true);

    dlg->signal_response().connect(
        sigc::bind(sigc::ptr_fun(&overwriteAnswered), dlg, done));

    dlg->present();
}
