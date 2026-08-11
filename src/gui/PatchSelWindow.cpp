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

#include "config.h"

#include <sstream>
#include <iostream>

#include <filesystem>
#include <system_error>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <gtkmm.h>

#include "think.h"

#include "PatchSelWindow.h"

#include "gthPrefs.h"
#include "gthPatchfile.h"
#include "Dialogs.h"


PatchSelWindow::PatchSelWindow (thSynth *argsynth)
     : dspAmp (Gtk::Adjustment::create(0, 0, MIDIVALMAX, 1, 10, 0),
              Gtk::Orientation::HORIZONTAL),
      browseButton("Browse"),
      saveButton("Save As"),
      unloadButton("Unload"),
      ampLabel("Amplitude"),
      fileLabel("Patch"),
      patchInfoExpander("Patch information"),
      /* No "Patch " on every one of them. They are inside a section called
         Patch information; saying it five more times is most of what made
         the panel look crowded. */
      patchRevisedLbl("Revised"),
      patchCategoryLbl("Category"),
      patchAuthorLbl("Author"),
      patchTitleLbl("Name"),
      patchCommentsLbl("Comments")
{
    currchan = -1;
    
    synth = argsynth;

    /* 475x400 cut the Amplitude column off the list and left the form below
       it with no room to be a form. */
    set_default_size(720, 620);

    set_title("thinksynth - Patch Selector");

    patchInfoExpander.set_child(patchInfoTable);

    /* A form: the label beside what it labels, right-aligned against it, in
       two columns of pairs.
     *
     * They used to sit centred *above* their fields, which is why the panel
       read as a scatter of boxes -- a caption over a field reads as a title
       for everything below it, and there were five of them doing that at
       once. Beside and right-aligned, the eye follows one line from the name
       to the value. */
    patchInfoTable.set_row_spacing(6);
    patchInfoTable.set_column_spacing(8);
    patchInfoTable.set_margin_top(8);
    patchInfoTable.set_margin_bottom(4);
    patchInfoTable.set_margin_start(12);

    {
        Gtk::Label *labels[] = { &patchTitleLbl, &patchCategoryLbl,
                                 &patchAuthorLbl, &patchRevisedLbl,
                                 &patchCommentsLbl };

        for (size_t i = 0; i < sizeof(labels) / sizeof(labels[0]); i++)
            labels[i]->set_xalign(1.0);
    }

    /* Comments is the one that wants the height, and it is aligned to the
       top of it rather than floating in the middle. */
    patchCommentsLbl.set_valign(Gtk::Align::START);
    patchCommentsWin.set_size_request(-1, 96);
    patchCommentsWin.set_has_frame(true);
    patchCommentsWin.set_hexpand(true);

    patchTitle.set_hexpand(true);
    patchCategory.set_hexpand(true);
    patchAuthor.set_hexpand(true);
    patchRevised.set_hexpand(true);

    /* Name and Author on the left, Category and Revised beside them: the two
       a person searches by first, then the two that describe it. */
    patchInfoTable.attach(patchTitleLbl,    0, 0, 1, 1);
    patchInfoTable.attach(patchTitle,       1, 0, 1, 1);
    patchInfoTable.attach(patchCategoryLbl, 2, 0, 1, 1);
    patchInfoTable.attach(patchCategory,    3, 0, 1, 1);
    patchInfoTable.attach(patchAuthorLbl,   0, 1, 1, 1);
    patchInfoTable.attach(patchAuthor,      1, 1, 1, 1);
    patchInfoTable.attach(patchRevisedLbl,  2, 1, 1, 1);
    patchInfoTable.attach(patchRevised,     3, 1, 1, 1);
    patchInfoTable.attach(patchCommentsLbl, 0, 2, 1, 1);
    patchInfoTable.attach(patchCommentsWin, 1, 2, 3, 1);

    patchComments.set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
    patchCommentsWin.set_child(patchComments);
    patchCommentsWin.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);

    patchScroll.set_child(patchView);
    patchScroll.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);

    /* Whichever row is current, however it got there. This hung off
       button-press, a signal a GTK4 TreeView does not have and which never
       saw a row selected with the arrow keys. */
    patchView.signal_cursor_changed().connect(
        sigc::mem_fun(*this, &PatchSelWindow::patchSelected));
    patchView.signal_cursor_changed().connect(
        sigc::mem_fun(*this, &PatchSelWindow::CursorChanged));

    patchScroll.set_vexpand(true);
    vbox.append(patchScroll);

    /* HScale(min, max, step) chose the displayed digits from the step, and it
       is the constructor GTK4 does not have. So the scale now says for itself
       what that was choosing: whole MIDI units. */
    dspAmp.set_digits(0);

    /* And the number is shown, the way the master and per-patch levels in the
       main window show theirs. A slider with no readout is a slider you have
       to guess at. */
    dspAmp.set_draw_value(true);
    dspAmp.set_value_pos(Gtk::PositionType::RIGHT);

    /* this is not to be used unless a valid amp arg is found */
    dspAmp.set_sensitive(false);

    patchModel = Gtk::ListStore::create (patchViewCols);
    patchView.set_model(patchModel);

    patchView.append_column("Channel", patchViewCols.chanNum);
    patchView.append_column("Patch", patchViewCols.dspName);
    patchView.append_column("Level", patchViewCols.amp);

    /* The full path on hover; the column itself shows the basename. */
    patchView.set_tooltip_column(3);

    /* The patch name takes the slack. Without this the last column does,
       which left Level a hand's width of empty and its number stranded at
       the far right of the window. */
    if (Gtk::TreeViewColumn *col = patchView.get_column(1))
        col->set_expand(true);

    /* A number belongs against the right of its column. */
    if (Gtk::TreeViewColumn *col = patchView.get_column(2))
    {
        col->set_alignment(1.0);

        std::vector<Gtk::CellRenderer *> cells = col->get_cells();

        for (size_t i = 0; i < cells.size(); i++)
            cells[i]->set_property("xalign", 1.0);
    }

    dspAmp.signal_value_changed().connect(
        sigc::mem_fun(*this, &PatchSelWindow::SetChannelAmp));

    fileEntry.signal_activate().connect(
        sigc::mem_fun(*this, &PatchSelWindow::fileEntryActivate));

    browseButton.signal_clicked().connect(
        sigc::mem_fun(*this, &PatchSelWindow::BrowsePatch));

    saveButton.signal_clicked().connect(
        sigc::mem_fun(*this, &PatchSelWindow::SavePatch));

    unloadButton.signal_clicked().connect(
        sigc::mem_fun(*this, &PatchSelWindow::UnloadDSP));

    /* The three things you can do to the selected patch, together and in the
       order they happen to it: find one, write it somewhere, take it off the
       channel. They were spread over two rows with the filename between
       them. */
    actionBox.set_spacing(6);
    actionBox.append(browseButton);
    actionBox.append(saveButton);
    actionBox.append(unloadButton);

    fileLabel.set_xalign(1.0);
    ampLabel.set_xalign(1.0);

    fileEntry.set_hexpand(true);
    dspAmp.set_hexpand(true);

    controlTable.set_row_spacing(6);
    controlTable.set_column_spacing(8);
    controlTable.set_margin_start(12);
    controlTable.set_margin_end(12);
    controlTable.set_margin_top(10);
    controlTable.set_margin_bottom(12);

    controlTable.attach(fileLabel,         0, 0, 1, 1);
    controlTable.attach(fileEntry,         1, 0, 1, 1);
    controlTable.attach(actionBox,         2, 0, 1, 1);
    controlTable.attach(ampLabel,          0, 1, 1, 1);
    controlTable.attach(dspAmp,            1, 1, 2, 1);
    controlTable.attach(patchInfoExpander, 0, 2, 3, 1);

    /* Filled first, parented second.
     *
     * These two used to be appended to the box above and populated
       afterwards, which GTK3 did not mind. GTK4 builds a CSS node per widget
       and threads them onto the parent's list as children arrive, and adding
       to a container that is already in a realised hierarchy is where
       gtk_css_node_insert_after's sibling assertion comes from. Building the
       grid before it has a parent means there is no list to thread onto
       yet. */
    /* One separator between the list and the panel below it, so the two read
       as two things rather than as a list that ran out. */
    vbox.append(*Gtk::manage(new Gtk::Separator(Gtk::Orientation::HORIZONTAL)));
    vbox.append(controlTable);

    set_child(vbox);

    gthPatchManager *patchMgr = gthPatchManager::instance();
    patchMgr->signal_patches_changed().connect(
        sigc::mem_fun(*this, &PatchSelWindow::onPatchesChanged));
}

PatchSelWindow::~PatchSelWindow (void)
{
}

void PatchSelWindow::UnloadDSP (void)
{
    Glib::RefPtr<Gtk::TreeView::Selection> refSelection =
        patchView.get_selection();

    if (refSelection)
    {
        Gtk::TreeModel::iterator iter;
        iter = refSelection->get_selected();

        if (iter)
        {
#if 0
              /* Delete the thMidiChan + modnode */
            thMidiChan *c = synth->getChannel((*iter)[patchViewCols.chanNum] - 1);
            synth->removeChan ((*iter)[patchViewCols.chanNum] - 1);

            if (c)
            {
                thSynthTree *m = c->GetMod();
                if (m)
                    delete m;
                delete c;
            }
#endif
            gthPatchManager *patchMgr = gthPatchManager::instance();

            /* the subsequent signal emitted by patchMgr ought to cause
               this object to repopulate itself */
             patchMgr->unloadPatch((*iter)[patchViewCols.chanNum]-1);

            /* After deletion, nothing will be highlighted, so disable
             * and clear things */
            patchRevised.set_text("");
            patchCategory.set_text("");
            patchAuthor.set_text("");
            patchTitle.set_text("");
            patchComments.get_buffer()->set_text("");
            
            fileEntry.set_text("");
            fileEntry.set_sensitive(false);
            browseButton.set_sensitive(false);
            unloadButton.set_sensitive(false);
            saveButton.set_sensitive(false);
            dspAmp.set_value(0);
            dspAmp.set_sensitive(false);
        }
    }
}

bool PatchSelWindow::LoadPatch (void)
{
    Glib::RefPtr<Gtk::TreeView::Selection> refSelection = 
        patchView.get_selection();

    if (refSelection)
    {
        Gtk::TreeModel::iterator iter;
        iter = refSelection->get_selected();

        if (iter)
        {
            int chanNum = (*iter)[patchViewCols.chanNum]-1;
            gthPatchManager *patchMgr = gthPatchManager::instance();

            /* the patchMgr should subsequently emit a signal that will cause
               PatchSelWindow to correct its own contents */
             if (patchMgr->loadPatch(fileEntry.get_text(), chanNum))
            {
                /* focus the new channel */
                std::ostringstream s;
                s << chanNum;

                gthPatchManager::PatchFile *patch =patchMgr->getPatch(chanNum);
                Gtk::TreeModel::Path p(s.str());
                patchView.set_cursor(p);

                /* load up metadata */
                patchRevised.set_text(patch->info["revised"]);
                patchCategory.set_text(patch->info["category"]);
                patchAuthor.set_text(patch->info["author"]);
                patchTitle.set_text(patch->info["title"]);
                patchComments.get_buffer()->set_text(patch->info["comments"]);

                return true;
            }
            else
            {
                /* error message handled in sighandler */
                fileEntry.set_text("");
                return false;
            }

        }
    }

    return false;
}

void PatchSelWindow::BrowsePatch (void)
{
    /* gtkmm-3 removed Gtk::FileSelection. FileChooserDialog has no buttons of
       its own, so the action area has to be populated explicitly; GTK4 then
       removed run(), so the dialog is shown here and answered in
       onBrowseResponse. */
    Gtk::FileChooserDialog *fileSel =
        new Gtk::FileChooserDialog(*this, "thinksynth - Load Patch",
                                   Gtk::FileChooser::Action::OPEN);

    fileSel->set_modal(true);
    fileSel->add_button("_Cancel", Gtk::ResponseType::CANCEL);
    fileSel->add_button("_Open", Gtk::ResponseType::OK);

    if (prevDir != "")
        fileSel->set_current_folder(Gio::File::create_for_path(prevDir));

    fileSel->signal_response().connect(
        sigc::bind(sigc::mem_fun(*this, &PatchSelWindow::onBrowseResponse),
                   fileSel));

    fileSel->present();
}

void PatchSelWindow::onBrowseResponse (int response,
                                       Gtk::FileChooserDialog *fileSel)
{
    const string picked = response == Gtk::ResponseType::OK
                          ? chosenPath(*fileSel) : string();

    closeDialog(fileSel);

    if (picked.empty())
        return;

    /* S_ISLNK does not exist on Windows, which has no POSIX symlink to test
       for -- and the stat() return value was being ignored here, so a file
       that vanished between the chooser and this line was judged on an
       uninitialised st_mode.
     *
     * fs::is_regular_file follows symlinks, so a link to a patch still
       passes, which is what the original was reaching for by accepting
       S_ISLNK unconditionally. It also answers false rather than misbehaving
       when the path has gone. */
    std::error_code ec;

    if (!std::filesystem::is_regular_file(picked, ec))
    {
        showError(this, "That is not a file that can be loaded", picked);
        return;
    }

    fileEntry.set_text(picked);

    if (!LoadPatch())
        return;

    prevDir = thUtil::dirname((char *)picked.c_str());

    string **vals = new string *[2];

    vals[0] = new string(prevDir);
    vals[1] = NULL;

    gthPrefs::instance()->Set("patchdir", vals);
}


void PatchSelWindow::SavePatch (void)
{
    Glib::RefPtr<Gtk::TreeView::Selection> refSelection =
        patchView.get_selection();

    if (!refSelection)
        return;

    Gtk::TreeModel::iterator iter = refSelection->get_selected();

    if (!iter)
        return;

    /* Which channel, decided now. The chooser is answered later and the
       selection can move in between -- it could not before, because run()
       held the rest of the window still. */
    const int chan = (*iter)[patchViewCols.chanNum] - 1;

    Gtk::FileChooserDialog *fileSel =
        new Gtk::FileChooserDialog(*this, "thinksynth - Save Patch",
                                   Gtk::FileChooser::Action::SAVE);

    fileSel->set_modal(true);
    fileSel->add_button("_Cancel", Gtk::ResponseType::CANCEL);
    fileSel->add_button("_Save", Gtk::ResponseType::OK);

    if (prevDir != "")
        fileSel->set_current_folder(Gio::File::create_for_path(prevDir));

    fileSel->signal_response().connect(
        sigc::bind(sigc::mem_fun(*this, &PatchSelWindow::onSaveResponse),
                   fileSel, chan));

    fileSel->present();
}

void PatchSelWindow::onSaveResponse (int response,
                                     Gtk::FileChooserDialog *fileSel, int chan)
{
    const string file = response == Gtk::ResponseType::OK
                        ? chosenPath(*fileSel) : string();

    closeDialog(fileSel);

    if (file.empty())
        return;

    /* GTK3's chooser asked before replacing a file; GTK4's does not, so it is
       asked here. */
    confirmOverwrite(this, file,
        sigc::bind(sigc::mem_fun(*this, &PatchSelWindow::writePatch),
                   file, chan));
}

void PatchSelWindow::writePatch (string file, int chan)
{
    gthPatchManager *patchManager = gthPatchManager::instance();
    gthPatchManager::PatchFile *patch = patchManager->getPatch(chan);

    if (patch == NULL)
        return;

    /* cull metadata */
    patch->info["revised"] = patchRevised.get_text();
    patch->info["category"] = patchCategory.get_text();
    patch->info["author"] = patchAuthor.get_text();
    patch->info["title"] = patchTitle.get_text();
    patch->info["comments"] = patchComments.get_buffer()->get_text();

    patchManager->savePatch(file, chan);

    /* update prefs file "prevDir" info */
    fileEntry.set_text(file);

    prevDir = thUtil::dirname(file.c_str());

    string **vals = new string *[2];

    vals[0] = new string(prevDir);
    vals[1] = NULL;

    gthPrefs::instance()->Set("patchdir", vals);
}

void PatchSelWindow::fileEntryActivate (void)
{
    LoadPatch ();
}

void PatchSelWindow::SetChannelAmp (void)
{
    Glib::RefPtr<Gtk::TreeView::Selection> refSelection = 
        patchView.get_selection();

    if (refSelection)
    {
        Gtk::TreeModel::iterator iter;
        iter = refSelection->get_selected();

        if (iter)
        {
            int chanNum = (*iter)[patchViewCols.chanNum] - 1;
            thArg *arg = new thArg("amp", dspAmp.get_value());

            (*iter)[patchViewCols.amp] =
                Glib::ustring::format((int)(dspAmp.get_value() + 0.5));

            synth->setChanArg(chanNum, arg);
        } 
    }
}

void PatchSelWindow::patchSelected (void)
{
    {
        Glib::RefPtr<Gtk::TreeView::Selection> refSelection = 
            patchView.get_selection();

        if (refSelection)
        {
            Gtk::TreeModel::iterator iter;
            /* Nothing to hit-test: the cursor has already
               moved to the row, which is what this now hears
               about. The old button-press binding had to work
               out which row had been hit for itself. */
        }
    }
}

void PatchSelWindow::CursorChanged (void)
{
    Glib::RefPtr<Gtk::TreeView::Selection> refSelection = 
        patchView.get_selection();
    gthPatchManager *patchMgr = gthPatchManager::instance();
    /* This is the OLD patch from the previously selected channel */
    gthPatchManager::PatchFile *oldpatch = NULL;
    bool loaded;
    
    if (currchan > 0)
        oldpatch = patchMgr->getPatch(currchan);

    if (refSelection)
    {
        Gtk::TreeModel::iterator iter;
        iter = refSelection->get_selected();

        /* save metadata from old patch */
        if (oldpatch)
        {
            oldpatch->info["revised"] = patchRevised.get_text();
            oldpatch->info["category"] = patchCategory.get_text();
            oldpatch->info["author"] = patchAuthor.get_text();
            oldpatch->info["title"] = patchTitle.get_text();
            oldpatch->info["comments"] = patchComments.get_buffer()->get_text();
        }

        if (iter)
        {
            currchan = (*iter)[patchViewCols.chanNum] - 1;

            gthPatchManager::PatchFile *patch = patchMgr->getPatch(currchan);

            /* make these widgets usable now that a valid row is
               selected */
            browseButton.set_sensitive(true);
            fileEntry.set_sensitive(true);

            /* Asked of the patch manager and the synth rather than read back
               out of the row. The row holds what the list shows -- a basename
               and a rounded level -- and putting a basename in the filename
               box would have been a path the Browse button could not
               reopen. */
            loaded = (patch != NULL);

            fileEntry.set_text(loaded ? patch->filename : string());

            {
                thArg *chanAmp = synth->getChanArg(currchan, "amp");

                if (chanAmp)
                    dspAmp.set_value((double)(*chanAmp)[0]);
            }

            /* Populate the metadata fields, or empty them.
             *
             * Emptying them is the half that was missing: selecting a free
               channel left the last patch's name and author sitting in the
               form, which read as though that channel had them. It mattered
               less when the panel was folded away and hard to look at. */
            patchTitle.set_text(loaded ? patch->info["title"] : string());
            patchCategory.set_text(loaded ? patch->info["category"] : string());
            patchAuthor.set_text(loaded ? patch->info["author"] : string());
            patchRevised.set_text(loaded ? patch->info["revised"] : string());
            patchComments.get_buffer()->set_text(
                loaded ? patch->info["comments"] : string());

            /* And the fields themselves are only worth typing in when there
               is a patch for them to describe. */
            patchInfoTable.set_sensitive(loaded);

            dspAmp.set_sensitive(loaded);
            unloadButton.set_sensitive(loaded);
            saveButton.set_sensitive(loaded);
        }
    }
    else
        currchan = -1;
}

void PatchSelWindow::populate (void)
{
//    std::map<int, string> *patchlist = synth->getPatchlist();
//    int channelcount = synth->midiChanCount();
    gthPatchManager *patchMgr = gthPatchManager::instance();
    int chancount = patchMgr->numPatches();
    int selectedChan = -1;
    Glib::RefPtr<Gtk::TreeView::Selection> refSelection = patchView.get_selection();

    /* save currently selected channel, if applicable */
    if (refSelection)
    {
        Gtk::TreeModel::iterator iter;
        iter = refSelection->get_selected();

        if (iter)
            selectedChan = (*iter)[patchViewCols.chanNum]-1;
    }
    
    patchModel->clear();

    for (int i = 0; i < chancount; i++)
    {
        Gtk::TreeModel::Row row = *(patchModel->append());
        gthPatchManager::PatchFile *patch = patchMgr->getPatch(i);

        /* A free channel: a number and two empty cells. It used to read
           0.000000 under Amplitude, which looks like a level of zero rather
           than like nothing being there. */
        row[patchViewCols.chanNum] = i + 1;
        row[patchViewCols.amp] = "";
        row[patchViewCols.dspName] = "";
        row[patchViewCols.path] = "";

        if (patch == NULL)
            continue;

        string filename = patch->filename;
        thArg *amp = synth->getChanArg(i, "amp");

        /* populate the controls with the data from the first row */
        if (i == 0)
        {
            fileEntry.set_text(filename);

            if (amp)
            {
                /* make the slider sensitive since there is an amp arg */
                dspAmp.set_sensitive(true);
                dspAmp.set_value((double)(*amp)[0]);
            }
        }

        row[patchViewCols.chanNum] = i + 1;
        row[patchViewCols.dspName] = filename.length() == 0
            ? "(Untitled)"
            : thUtil::basename((char *)filename.c_str());
        row[patchViewCols.path] = filename;
        row[patchViewCols.amp] = amp
            ? Glib::ustring::format((int)((*amp)[0] + 0.5f))
            : Glib::ustring();
    }

    if (selectedChan != -1)
    {
        std::ostringstream s;
        s << selectedChan;
        Gtk::TreeModel::Path p(s.str());
        patchView.set_cursor(p);
    }
}

void PatchSelWindow::on_realize(void)
{
    Gtk::Window::on_realize();

    gthPrefs *prefs = gthPrefs::instance();
    
    if (prefs)
    {
        string **vals = prefs->Get("patchdir");

        if (vals)
        {
            prevDir = *vals[0];
        }
        else
        {
            prevDir = DSP_PATH;
            prevDir += "/patches";
        }
    }
    else
    {
        prevDir = DSP_PATH;
        prevDir += "/patches";
    }
    
    populate();
}

void PatchSelWindow::onPatchesChanged (void)
{
    populate();
}
