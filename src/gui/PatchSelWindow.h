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
    }

    Gtk::TreeModelColumn <unsigned int> chanNum;
    Gtk::TreeModelColumn <Glib::ustring> dspName;
    Gtk::TreeModelColumn <float> amp;
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
    void SavePatch (void);
    void CursorChanged (void);
    void UnloadDSP (void);

    void patchSelected (GdkEventButton *);
    void fileEntryActivate (void);
    void onPatchesChanged (void);
    
    /* Overloaded GTK-- sighandler */
    virtual void on_realize (void);

    Gtk::VBox vbox;
    Gtk::Table controlTable;

    Gtk::HScale dspAmp;
    Gtk::Button setButton;
    Gtk::Button browseButton;
    Gtk::Button saveButton;
    Gtk::Button unloadButton;
    Gtk::Label ampLabel;

    Gtk::Label fileLabel;
    Gtk::Entry fileEntry;

    Gtk::Expander patchInfoExpander;
    Gtk::Table patchInfoTable;
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
