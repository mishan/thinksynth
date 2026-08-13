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

#ifndef PATCHSEL_WINDOW_H
#define PATCHSEL_WINDOW_H

#include "SaveButton.h"

/* One channel's row.
 *
 * `chan' is zero-based, the way the engine counts, and the column adds one for
 * display. The ListStore this replaces held the display number and every
 * reader subtracted one back off it -- six places to get a fencepost wrong
 * instead of one.
 */
class PatchSelRow : public Glib::Object
{
public:
    static Glib::RefPtr<PatchSelRow> create (int chan)
    {
        return Glib::make_refptr_for_instance(new PatchSelRow(chan));
    }

    int chan (void) const { return chan_; }

    Glib::ustring dspName (void) const { return dspName_; }
    void setDspName (const Glib::ustring &s) { dspName_ = s; }

    /* Text, not a number. A free channel has no amplitude, and a column of
       0.000000 down thirteen empty rows is noise that reads like data. Empty
       means empty; a loaded channel gets a whole number, which is what the
       0..127 scale is in. */
    Glib::ustring amp (void) const { return amp_; }
    void setAmp (const Glib::ustring &s) { amp_ = s; }

    /* The full path, for the row's tooltip. The column shows the basename --
       every patch in a library shares its directory, so the part that
       identifies one is the part a narrow column cuts off. */
    Glib::ustring path (void) const { return path_; }
    void setPath (const Glib::ustring &s) { path_ = s; }

protected:
    PatchSelRow (int chan)
        : Glib::ObjectBase(typeid(PatchSelRow)), chan_(chan) { }

private:
    int chan_;
    Glib::ustring dspName_;
    Glib::ustring amp_;
    Glib::ustring path_;
};

class PatchSelWindow : public Gtk::Window
{
public:
    PatchSelWindow (thSynth *);
    ~PatchSelWindow (void);

protected:
    bool LoadPatch (void);
    void SetChannelAmp (void);
    void BrowsePatch (void);

    /* The other half of each chooser: GTK4 answers a dialog after the
       function that opened it has returned. */
    void onBrowseResponse (int response, Gtk::FileChooserDialog *fileSel);
    void onSaveResponse (int response, Gtk::FileChooserDialog *fileSel,
                         int chan);
    void writePatch (string file, int chan);
    void SavePatch (void);

    /* Save, as opposed to Save As: straight back to the file the patch came
       from. */
    void SaveOverPatch (void);

    /* The filename box, scrolled so the end of the path is what shows. */
    void showPath (const string &path);
    void CursorChanged (void);
    void UnloadDSP (void);

    void patchSelected (void);
    void onInfoEdited (void);
    void onPatchDirty (int chan);
    void fileEntryActivate (void);
    void onPatchesChanged (void);
    
    /* Overloaded GTK-- sighandler */
    virtual void on_realize (void);

    Gtk::Box vbox{Gtk::Orientation::VERTICAL};

    /* One grid for everything below the list: the patch, its level, and the
       information panel. It was three strips stacked up, each with its own
       idea of alignment and spacing. */
    Gtk::Grid controlTable;
    Gtk::Box actionBox{Gtk::Orientation::HORIZONTAL};

    Gtk::Scale dspAmp{Gtk::Orientation::HORIZONTAL};
    Gtk::Button setButton;
    Gtk::Button browseButton;
    SaveButton saveButton;
    Gtk::Button unloadButton;
    Gtk::Label ampLabel;

    Gtk::Label fileLabel;
    Gtk::Entry fileEntry;

    Gtk::Expander patchInfoExpander;
    Gtk::Grid patchInfoTable;
    Gtk::Label patchRevisedLbl;
    Gtk::Entry patchRevised;
    Gtk::Label patchCategoryLbl;
    Gtk::Entry patchCategory;
    Gtk::Label patchAuthorLbl;
    Gtk::Entry patchAuthor;
    Gtk::Label patchTitleLbl;
    Gtk::Entry patchTitle;
    Gtk::Label patchCommentsLbl;
    Gtk::ScrolledWindow patchCommentsWin;
    Gtk::TextView patchComments;

    Gtk::ScrolledWindow patchScroll;
    Gtk::ColumnView patchView;
    Glib::RefPtr<Gio::ListStore<PatchSelRow> > patchModel;
    Glib::RefPtr<Gtk::SingleSelection> patchSelection;

    /* The row the user is on, or NULL. Every caller wanted the same three
       lines of "is there a selection, and what is in it". */
    Glib::RefPtr<PatchSelRow> selectedRow (void) const;

private:
    void populate (void);

    thSynth *synth;

    /* True while this window is filling the form from a patch, so that doing
       so is not mistaken for someone typing in it. */
    bool loading_;
    string prevDir;

    int currchan;
};

#endif /* PATCHSEL_WINDOW_H */
