/* $Id: PatchSelWindow.cpp,v 1.27 2004/04/09 07:29:40 misha Exp $ */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>

#include <gtkmm.h>

#include "think.h"

#include "PatchSelWindow.h"

PatchSelWindow::PatchSelWindow (thSynth *argsynth)
 	: dspAmp (0, MIDIVALMAX, .5),
//	  setButton("Load Patch"), 
	  browseButton("Browse"),
	  ampLabel("Amplitude"),
	  fileLabel("Filename")
{
	prevDir = NULL;
	synth = argsynth;

	set_default_size(550, 400);

	set_title("thinksynth - Patch Selector");

	add(vbox);

	patchScroll.add(patchView);
	patchScroll.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);

	patchView.signal_button_press_event().connect_notify(
		SigC::slot(*this, &PatchSelWindow::patchSelected));
	patchView.signal_cursor_changed().connect(
		SigC::slot(*this, &PatchSelWindow::CursorChanged));

	vbox.pack_start(patchScroll);

	/* this is not to be used unless a valid amp arg is found */
	dspAmp.set_sensitive(false);

	std::map<int, string> *patchlist = synth->GetPatchlist();
	int channelcount = synth->GetChannelCount();

	patchModel = Gtk::ListStore::create (patchViewCols);
	patchView.set_model(patchModel);

	for(int i = 0; i < channelcount; i++)
	{
		Gtk::TreeModel::Row row = *(patchModel->append());
		string filename = (*patchlist)[i];
		thArg *amp = synth->GetChanArg(i, "amp");

		/* populate the fields with the data from the first row */
		if (i == 0)
		{
			fileEntry.set_text(filename);

			if (amp)
			{
				/* make the slider sensitive since there is an amp arg */
				dspAmp.set_sensitive(true);
				dspAmp.set_value((double)amp->argValues[0]);
			}
		}

		row[patchViewCols.chanNum] = i + 1;
		row[patchViewCols.dspName] = filename;

		if (amp)
		{
			row[patchViewCols.amp] = amp->argValues[0];
		}
		else
		{
			row[patchViewCols.amp] = 0;
		}
	}

	patchView.append_column("Channel", patchViewCols.chanNum);
	patchView.append_column("Filename", patchViewCols.dspName);
	patchView.append_column("Amplitude", patchViewCols.amp);
	
	dspAmp.signal_value_changed().connect(
		SigC::slot(*this, &PatchSelWindow::SetChannelAmp));

	fileEntry.signal_activate().connect(
		SigC::slot(*this, &PatchSelWindow::fileEntryActivate));

	browseButton.signal_clicked().connect(
		SigC::slot(*this, &PatchSelWindow::BrowsePatch));

	vbox.pack_start(controlTable, Gtk::PACK_SHRINK, 5);

	controlTable.attach(ampLabel, 0, 1, 0, 1, Gtk::SHRINK, Gtk::SHRINK, 5, 0);
	controlTable.attach(dspAmp, 1, 2, 0, 1);
	controlTable.attach(fileLabel, 0, 1, 1, 2, Gtk::SHRINK,
						Gtk::SHRINK, 0, 5);
	controlTable.attach(fileEntry, 1, 2, 1, 2, Gtk::FILL|Gtk::EXPAND,
						Gtk::FILL|Gtk::EXPAND, 0, 5);
	controlTable.attach(browseButton, 2, 3, 1, 2, Gtk::SHRINK, Gtk::SHRINK, 5,
						0);
//	controlTable.attach(setButton, 0, 1, 2, 3, Gtk::SHRINK, Gtk::SHRINK, 5, 5);
}

PatchSelWindow::~PatchSelWindow (void)
{
	if (prevDir)
		free (prevDir);
}

bool PatchSelWindow::LoadPatch (void)
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
			thMod *loaded = NULL;

			if ((loaded = synth->LoadMod(fileEntry.get_text().c_str(),
											 chanNum, (float)dspAmp.get_value()
					 )) == NULL)
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
			else
			{
				(*iter)[patchViewCols.dspName] = fileEntry.get_text();

				dspAmp.set_sensitive(true);

				return true;
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
			thArg *arg = new thArg("amp", val, 1);

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

			patchView.get_path_at_pos((int)b->x, (int)b->y, path, col, 
									  cell_x, cell_y);
				
			refSelection->select(path);
		}
	}
}

void PatchSelWindow::CursorChanged (void)
{
	Glib::RefPtr<Gtk::TreeView::Selection> refSelection = 
		patchView.get_selection();

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
//			setButton.set_sensitive(true);

			fileEntry.set_text(filename);
			dspAmp.set_value((double)amp);

			/* if no DSP is loaded, then don't touch the amplitude */
			if (filename == "")
			{
				dspAmp.set_sensitive(false);
			}
			else
			{
				dspAmp.set_sensitive(true);
			}

		} 
	}
}
