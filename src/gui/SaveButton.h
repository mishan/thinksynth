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

#ifndef SAVE_BUTTON_H
#define SAVE_BUTTON_H

/*
 * Save, with Save As behind an arrow.
 *
 * Two buttons side by side spend twice the width on the one you rarely want,
 * and put it next to the one you always want, which is how a Save As gets
 * clicked by accident. This is the usual answer: the common action is the
 * button, the variant is one click further away.
 *
 * GTK4 has no split-button widget -- that is AdwSplitButton, and libadwaita is
 * not a dependency here -- so it is a Button and a MenuButton in a box with
 * the "linked" style class, which is what makes the two draw as one control.
 *
 * The menu is a popover holding a button rather than a Gio::Menu and an
 * action. A menu model would mean an action group, a name, and a prefix, for
 * one item that already has a signal to emit.
 */
class SaveButton : public Gtk::Box
{
public:
    SaveButton (void);

    /* Clicking Save. When there is nothing to save over -- a patch that has
       never been written anywhere -- this is not emitted and signal_save_as()
       is, because Save As is what Save means for a file with no name yet. */
    sigc::signal<void ()> &signal_save (void) { return m_signal_save_; }
    sigc::signal<void ()> &signal_save_as (void) { return m_signal_save_as_; }

    /* Whether there is a file to save over. False makes Save fall through to
       Save As rather than making it dead: a control that does nothing and
       explains itself in a tooltip is a worse answer than one that does the
       nearest useful thing. */
    void setHasFile (bool hasFile);

    /* Whether anything has changed since the patch was loaded or saved.
     *
     * False greys Save and leaves Save As alone -- an unmodified patch has
     * nothing to write back, but saving it somewhere else is still a
     * perfectly good thing to want. */
    void setModified (bool modified);

    /* Where Save would write, for the tooltip. Setting one on the SaveButton
       itself does not work: a tooltip belongs to the widget under the pointer,
       and that is the Save button inside, which has its own. */
    void setFileName (const std::string &path);

    /* Both halves at once, for when there is no patch at all. */
    void setSensitive (bool sensitive);

protected:
    void onSave (void);
    void onSaveAs (void);

    Gtk::Button save_;
    Gtk::MenuButton more_;
    Gtk::Popover menu_;
    Gtk::Button saveAs_;

    /* Save is dead only when there is both a file to write over and nothing
       to write; with no file it means Save As, which is never pointless. */
    void refresh (void);

    bool hasFile_;
    bool modified_;
    bool sensitive_;
    std::string path_;

    sigc::signal<void ()> m_signal_save_;
    sigc::signal<void ()> m_signal_save_as_;
};

#endif /* SAVE_BUTTON_H */
