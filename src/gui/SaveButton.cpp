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

#include <gtkmm.h>

#include "SaveButton.h"

SaveButton::SaveButton (void)
    : Gtk::Box(Gtk::Orientation::HORIZONTAL),
      save_("_Save", true), saveAs_("Save _As...", true),
      hasFile_(false), modified_(true), sensitive_(true)
{
    /* What draws the two as one control: rounded on the outside, square where
       they meet. */
    add_css_class("linked");

    more_.set_tooltip_text("Other ways to save");

    /* The popover's own button is flat, so it reads as a menu item rather
       than as a button inside a bubble. */
    saveAs_.add_css_class("flat");
    saveAs_.set_has_frame(false);

    menu_.set_child(saveAs_);
    menu_.set_has_arrow(true);

    more_.set_popover(menu_);

    save_.signal_clicked().connect(
        sigc::mem_fun(*this, &SaveButton::onSave));
    saveAs_.signal_clicked().connect(
        sigc::mem_fun(*this, &SaveButton::onSaveAs));

    append(save_);
    append(more_);

    setHasFile(false);
}

void SaveButton::onSave (void)
{
    /* Save As is what Save means when there is no file yet. */
    if (hasFile_)
        m_signal_save_.emit();
    else
        m_signal_save_as_.emit();
}

void SaveButton::onSaveAs (void)
{
    menu_.popdown();

    m_signal_save_as_.emit();
}

void SaveButton::setHasFile (bool hasFile)
{
    hasFile_ = hasFile;

    refresh();
}

void SaveButton::setModified (bool modified)
{
    modified_ = modified;

    refresh();
}

void SaveButton::setFileName (const std::string &path)
{
    path_ = path;

    refresh();
}

void SaveButton::setSensitive (bool sensitive)
{
    sensitive_ = sensitive;

    refresh();
}

void SaveButton::refresh (void)
{
    /* Save As is live whenever there is anything at all to save. Save is
       live when there is something to write and somewhere it has not already
       been written -- or when there is no file yet, because then it means
       Save As. */
    const bool canSave = sensitive_ && (!hasFile_ || modified_);

    save_.set_sensitive(canSave);
    more_.set_sensitive(sensitive_);

    /* Four states, and the tooltip says which one it is in. Nothing selected
       came first: describing a file when there is no patch at all was the
       one that could actively mislead. */
    if (!sensitive_)
        save_.set_tooltip_text("Nothing to save");
    else if (!hasFile_)
        save_.set_tooltip_text("This has not been saved yet -- Save will ask "
                               "where to put it");
    else if (!modified_)
        save_.set_tooltip_text("Nothing has changed since this was saved");
    else
        save_.set_tooltip_text(path_.empty()
                               ? std::string("Save the changes")
                               : "Save the changes to " + path_);
}
