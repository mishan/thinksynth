/* $Id: PatchSelWindow.cpp,v 1.44 2004/11/16 23:22:02 misha Exp $ */
/*
 * Copyright (C) 2004 Metaphonic Labs
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
	  fileLabel("Filename")
{
	synth = argsynth;

	set_default_size(475, 400);

	set_title("thinksynth - Patch Selector");

	add(vbox);

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

	populate ();

	patchView.append_column("Channel", patchViewCols.chanNum);
	patchView.append_column("Filename", patchViewCols.dspName);
	patchView.append_column("Amplitude", patchViewCols.amp);

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
			prevDir = strdup(DSP_PATH);
		}
	}
	else
	{
		prevDir = strdup(DSP_PATH);
	}

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
			thMidiChan *c = synth->GetChannel((*iter)[patchViewCols.chanNum] - 1);
			synth->removeChan ((*iter)[patchViewCols.chanNum] - 1);

			if (c)
			{
				thMod *m = c->GetMod();
				if (m)
					delete m;
				delete c;
			}
#endif
			gthPatchManager *patchMgr = gthPatchManager::instance();

			/* the subsequent signal emitted by patchMgr ought to cause
			   this object to repopulate itself */
 			patchMgr->unloadPatch((*iter)[patchViewCols.chanNum]-1);
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
				return true;
			}
			else
			{
				char *error = g_strdup_printf("Couldn't load %s: %s",
											  fileEntry.get_text().c_str(),
											  strerror(errno));
				Gtk::MessageDialog errorDialog (error, Gtk::MESSAGE_ERROR);

				errorDialog.run();
				g_free(error);
				dspAmp.set_sensitive(false);

				return false;
			}

		}
	}

	return false;
}

void PatchSelWindow::BrowsePatch (void)
{
	Gtk::FileSelection fileSel("thinksynth - Load Patch");

	if (prevDir)
		fileSel.set_filename(prevDir);

	if(fileSel.run() == Gtk::RESPONSE_OK)
	{
		fileEntry.set_text(fileSel.get_filename());

		if(LoadPatch())
		{
			char *file = strdup(fileSel.get_filename().c_str());

			if (prevDir)
				free (prevDir);

			prevDir = g_strdup_printf("%s/", dirname(file));
			free (file);

			string **vals = new string *[2];
			vals[0] = new string(prevDir);
			vals[1] = NULL;

			gthPrefs *prefs = gthPrefs::instance();
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

		if(fileSel.run() == Gtk::RESPONSE_OK)
		{
			patchManager->savePatch(fileSel.get_filename(),
									(*iter)[patchViewCols.chanNum]-1);

			/* update prefs file "prevDir" info */
			fileEntry.set_text(fileSel.get_filename());

			char *file = strdup(fileSel.get_filename().c_str());

			if (prevDir)
				free (prevDir);

			prevDir = g_strdup_printf("%s/", dirname(file));
			free (file);

			string **vals = new string *[2];
			vals[0] = new string(prevDir);
			vals[1] = NULL;
		
			gthPrefs *prefs = gthPrefs::instance();
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

	if(refSelection)
	{
		Gtk::TreeModel::iterator iter;
		iter = refSelection->get_selected();

		if(iter)
		{
			int chanNum = (*iter)[patchViewCols.chanNum] - 1;
			float *val = new float;
			*val = (float)dspAmp.get_value();
			thArg *arg = new thArg(string("amp"), val, 1);

			(*iter)[patchViewCols.amp] = *val;

			synth->SetChanArg(chanNum, arg);
		} 
	}
}

void PatchSelWindow::patchSelected (GdkEventButton *b)
{
	if(b && b->type == GDK_BUTTON_PRESS)
	{
		Glib::RefPtr<Gtk::TreeView::Selection> refSelection = 
			patchView.get_selection();

		if(refSelection)
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
	bool loaded;

	if(refSelection)
	{
		Gtk::TreeModel::iterator iter;
		iter = refSelection->get_selected();

		if(iter)
		{
			Glib::ustring filename = (*iter)[patchViewCols.dspName];
			float amp = (*iter)[patchViewCols.amp];

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

			dspAmp.set_sensitive(loaded);
			unloadButton.set_sensitive(loaded);
			saveButton.set_sensitive(loaded);
		} 
	}
}

void PatchSelWindow::populate (void)
{
	debug("populating contents");

//	std::map<int, string> *patchlist = synth->GetPatchlist();
//	int channelcount = synth->GetChannelCount();
	gthPatchManager *patchMgr = gthPatchManager::instance();
	int chancount = patchMgr->numPatches();

	patchModel->clear();

	for(int i = 0; i < chancount; i++)
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
		thArg *amp = synth->GetChanArg(i, "amp");

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
}

void PatchSelWindow::onPatchesChanged (void)
{
	populate();
}
