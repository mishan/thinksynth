/*
 * Copyright (C) 2004-2014 Metaphonic Labs
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

class PatchSelColumns : public Gtk::TreeModel::ColumnRecord
{
public:
    PatchSelColumns (void)
    {
        add (chanNum);
        add (dspName);
        add (amp);
        add (path);
    }

    Gtk::TreeModelColumn <unsigned int> chanNum;
    Gtk::TreeModelColumn <Glib::ustring> dspName;

    /* Text, not a number. A free channel has no amplitude, and a column of
       0.000000 down thirteen empty rows is noise that reads like data. Empty
       means empty; a loaded channel gets a whole number, which is what the
       0..127 scale is in. */
    Gtk::TreeModelColumn <Glib::ustring> amp;

    /* The full path, for the row's tooltip. The column shows the basename --
       every patch in a library shares its directory, so the part that
       identifies one is the part a narrow column cuts off. */
    Gtk::TreeModelColumn <Glib::ustring> path;
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
    void CursorChanged (void);
    void UnloadDSP (void);

    void patchSelected (void);
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
    Gtk::Button saveButton;
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
    Gtk::TreeView patchView;
    Glib::RefPtr<Gtk::ListStore> patchModel;
    PatchSelColumns patchViewCols;

private:
    void populate (void);

    thSynth *synth;
    string prevDir;

    int currchan;
};

#endif /* PATCHSEL_WINDOW_H */
