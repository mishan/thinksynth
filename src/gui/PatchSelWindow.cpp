/* $Id$ */
/*
 * Copyright (C) 2004-2005 Metaphonic Labs
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

#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>

#include <gtkmm.h>

#include "think.h"

#include "PatchSelWindow.h"

#include "gthPrefs.h"
#include "gthPatchfile.h"

PatchSelWindow::PatchSelWindow (thSynth *argsynth)
 	: dspAmp (0, MIDIVALMAX, 1),
	  browseButton("Browse"),
	  saveButton("Save As"),
	  unloadButton("Unload"),
	  ampLabel("Amplitude"),
	  fileLabel("Filename"),
	  patchInfoExpander("Patch information"),
	  patchInfoTable(5, 2),
	  patchRevisedLbl("Last revised:"),
	  patchCategoryLbl("Patch category:"),
	  patchAuthorLbl("Patch author:"),
	  patchTitleLbl("Patch name:"),
	  patchCommentsLbl("Patch description/comments:")
{
	currchan = -1;
	
	synth = argsynth;

	set_default_size(475, 400);

	set_title("thinksynth - Patch Selector");

	add(vbox);

	patchInfoExpander.add(patchInfoTable);

	patchInfoTable.attach(patchRevisedLbl,
		0, 1, 0, 1, Gtk::SHRINK, Gtk::SHRINK);
	patchInfoTable.attach(patchRevised,
		0, 1, 1, 2, Gtk::FILL, Gtk::FILL, 5, 0);
	patchInfoTable.attach(patchCategoryLbl,
		1, 2, 0, 1, Gtk::SHRINK, Gtk::SHRINK);
	patchInfoTable.attach(patchCategory,
		1, 2, 1, 2, Gtk::FILL, Gtk::FILL, 5, 0);
	patchInfoTable.attach(patchAuthorLbl,
		0, 1, 2, 3, Gtk::SHRINK, Gtk::SHRINK);
	patchInfoTable.attach(patchAuthor,
		0, 1, 3, 4, Gtk::FILL, Gtk::FILL, 5, 0);
	patchInfoTable.attach(patchTitleLbl,
		1, 2, 2, 3, Gtk::SHRINK, Gtk::SHRINK);
	patchInfoTable.attach(patchTitle,
		1, 2, 3, 4, Gtk::FILL, Gtk::FILL, 5, 0);
	patchInfoTable.attach(patchCommentsLbl,
		0, 2, 4, 5, Gtk::SHRINK, Gtk::SHRINK);
	patchInfoTable.attach(patchCommentsWin,
		0, 2, 5, 7, Gtk::FILL, Gtk::FILL, 5, 0); 
		
	patchComments.set_wrap_mode(Gtk::WRAP_WORD_CHAR);
	patchCommentsWin.add(patchComments);
	patchCommentsWin.set_shadow_type(Gtk::SHADOW_IN);
	patchCommentsWin.set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);

	patchScroll.add(patchView);
	patchScroll.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);

	patchView.signal_button_press_event().connect_notify(
		sigc::mem_fun(*this, &PatchSelWindow::patchSelected));
	patchView.signal_cursor_changed().connect(
		sigc::mem_fun(*this, &PatchSelWindow::CursorChanged));

	vbox.pack_start(patchScroll);

	/* this is not to be used unless a valid amp arg is found */
	dspAmp.set_sensitive(false);

	patchModel = Gtk::ListStore::create (patchViewCols);
	patchView.set_model(patchModel);

	patchView.append_column("Channel", patchViewCols.chanNum);
	patchView.append_column("Filename", patchViewCols.dspName);
	patchView.append_column("Amplitude", patchViewCols.amp);

	signal_realize().connect(sigc::mem_fun(*this, &PatchSelWindow::onRealize));

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
	
	vbox.pack_start(patchInfoExpander, Gtk::PACK_SHRINK);
	vbox.pack_start(controlTable, Gtk::PACK_SHRINK, 5);

	controlTable.attach(ampLabel, 0, 1, 0, 1, Gtk::SHRINK, Gtk::SHRINK, 5, 0);
	controlTable.attach(dspAmp, 1, 2, 0, 1);
	controlTable.attach(saveButton, 2, 4, 0, 1, Gtk::SHRINK, Gtk::SHRINK, 5, 0);
	controlTable.attach(fileLabel, 0, 1, 1, 2, Gtk::SHRINK,
						Gtk::SHRINK, 0, 5);
	controlTable.attach(fileEntry, 1, 2, 1, 2, Gtk::FILL|Gtk::EXPAND,
						Gtk::FILL|Gtk::EXPAND, 0, 5);
	controlTable.attach(browseButton, 2, 3, 1, 2, Gtk::SHRINK, Gtk::SHRINK, 5,
						0); 
	controlTable.attach(unloadButton, 3, 4, 1, 2, Gtk::SHRINK, Gtk::SHRINK, 5, 0); 
	
	gthPatchManager *patchMgr = gthPatchManager::instance();
	patchMgr->signal_patches_changed().connect(
		sigc::mem_fun(*this, &PatchSelWindow::onPatchesChanged));
}

PatchSelWindow::~PatchSelWindow (void)
{
	if (prevDir)
		free (prevDir);
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
				char* cstr = g_strdup_printf("%d", chanNum);
				gthPatchManager::PatchFile *patch = patchMgr->getPatch(chanNum);
				Gtk::TreeModel::Path p(cstr);
				patchView.set_cursor(p);
				free(cstr);

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
	Gtk::FileSelection fileSel ("thinksynth - Load Patch");

	if (prevDir)
		fileSel.set_filename(prevDir);

	if (fileSel.run() == Gtk::RESPONSE_OK)
	{
		struct stat st;
		stat(fileSel.get_filename().c_str(), &st);

		if (!S_ISLNK(st.st_mode) && !S_ISREG(st.st_mode))
		{
			fprintf(stderr,"error box...\n");
			return;
		}

		fileEntry.set_text(fileSel.get_filename());

		if (LoadPatch())
		{
			char *file = strdup(fileSel.get_filename().c_str());
			string **vals = NULL;
			gthPrefs *prefs = gthPrefs::instance();

			if (prevDir)
				free (prevDir);

			prevDir = g_strdup_printf("%s/", dirname(file));
			free (file);

			vals = new string *[2];
			vals[0] = new string(prevDir);
			vals[1] = NULL;

			prefs->Set("patchdir", vals);
		}
	}
}

void PatchSelWindow::SavePatch (void)
{
	gthPatchManager *patchManager = gthPatchManager::instance();
	Gtk::FileSelection fileSel("thinksynth - Save Patch");
	Glib::RefPtr<Gtk::TreeView::Selection> refSelection = 
		patchView.get_selection();

	if (refSelection)
	{
		Gtk::TreeModel::iterator iter;
		iter = refSelection->get_selected();

		if (prevDir)
			fileSel.set_filename(prevDir);
		
		if (fileSel.run() == Gtk::RESPONSE_OK)
		{
			gthPatchManager::PatchFile *patch = NULL;
			gthPrefs *prefs = gthPrefs::instance();
			char *file = (char*)fileSel.get_filename().c_str();
			int chan = (*iter)[patchViewCols.chanNum]-1;
			string **vals = NULL;

			/* cull metadata */
			patch = patchManager->getPatch(chan);

			patch->info["revised"] = patchRevised.get_text();
			patch->info["category"] = patchCategory.get_text();
			patch->info["author"] = patchAuthor.get_text();
			patch->info["title"] = patchTitle.get_text();
			patch->info["comments"] = patchComments.get_buffer()->get_text();
			
			patchManager->savePatch(file, chan);

			/* update prefs file "prevDir" info */
			fileEntry.set_text(file);

			/* update patch window with new name */
			(*iter)[patchViewCols.dspName] = strdup(basename(file));

			if (prevDir)
				free (prevDir);

			prevDir = g_strdup_printf("%s/", dirname(file));

			vals = new string *[2];
			vals[0] = new string(prevDir);
			vals[1] = NULL;
		
			prefs->Set("patchdir", vals);
		}
	}
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

			(*iter)[patchViewCols.amp] = dspAmp.get_value();

			synth->setChanArg(chanNum, arg);
		} 
	}
}

void PatchSelWindow::patchSelected (GdkEventButton *b)
{
	if (b && b->type == GDK_BUTTON_PRESS)
	{
		Glib::RefPtr<Gtk::TreeView::Selection> refSelection = 
			patchView.get_selection();

		if (refSelection)
		{
			Gtk::TreeModel::iterator iter;
			Gtk::TreeModel::Path path;
			Gtk::TreeViewColumn *col;
			int cell_x, cell_y; 

			if (patchView.get_path_at_pos((int)b->x, (int)b->y, path, col, 
									  cell_x, cell_y))
				refSelection->select(path);
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
			Glib::ustring filename = (*iter)[patchViewCols.dspName];
			float amp = (*iter)[patchViewCols.amp];
			currchan = (*iter)[patchViewCols.chanNum] - 1;
			gthPatchManager::PatchFile *patch = patchMgr->getPatch(currchan);

			/* make these widgets usable now that a valid row is
			   selected */
			browseButton.set_sensitive(true);
			fileEntry.set_sensitive(true);

			fileEntry.set_text(filename);
			dspAmp.set_value((double)amp);

			/* if no DSP is loaded, then don't touch the amplitude,
			 * and gray out the unload button because there's nothing
			 * to unload (except in your face) */
			loaded = (filename != "");

			/* populate metadata fields */
			if (loaded)
			{
				patchRevised.set_text(patch->info["revised"]);
				patchCategory.set_text(patch->info["category"]);
				patchAuthor.set_text(patch->info["author"]);
				patchTitle.set_text(patch->info["title"]);
				patchComments.get_buffer()->set_text(patch->info["comments"]);
			}

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
//	std::map<int, string> *patchlist = synth->getPatchlist();
//	int channelcount = synth->midiChanCount();
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

		/* reset initial values */
		row[patchViewCols.chanNum] = i + 1;
		row[patchViewCols.amp] = 0;
		row[patchViewCols.dspName] = "";

		if (patch == NULL)
			continue;

		string filename = patch->filename;
		thArg *amp = synth->getChanArg(i, "amp");

		/* populate the fields with the data from the first row */
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
		row[patchViewCols.dspName] = filename.length() == 0 ? 
			"(Untitled)" : filename;
		row[patchViewCols.amp] = amp ? (*amp)[0] : 0;
	}

	if (selectedChan != -1)
	{
		char *cstr = g_strdup_printf("%d", selectedChan);
		Gtk::TreeModel::Path p(cstr);
		patchView.set_cursor(p);
		free(cstr);
	}
}

void PatchSelWindow::onRealize(void)
{
	gthPrefs *prefs = gthPrefs::instance();
	
	if (prefs)
	{
		string **vals = prefs->Get("patchdir");

		if (vals)
		{
			prevDir = strdup(vals[0]->c_str());
		}
		else
		{
			prevDir = strdup(DSP_PATH "/patches");
		}
	}
	else
	{
		prevDir = strdup(DSP_PATH "/patches");
	}
	
	populate();
}

void PatchSelWindow::onPatchesChanged (void)
{
	populate();
}
