/* $Id: MainSynthWindow.cpp,v 1.52 2004/11/25 02:28:45 joshk Exp $ */
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
#include <sys/types.h>
#include <signal.h>
#include <libgen.h>

#include <gtkmm.h>
#include <gtkmm/messagedialog.h>

#include "think.h"

#include "PatchSelWindow.h"
#include "Keyboard.h"
#include "KeyboardWindow.h"
#include "MainSynthWindow.h"
#include "AboutBox.h"
#include "MidiMap.h"
#include "ArgTable.h"

#ifdef HAVE_JACK
# include "../gthJackAudio.h"
#endif

#include "../gthPrefs.h"
#include "../gthPatchfile.h"

/* SUPER XXX */
bool chosen = false;
extern Glib::Mutex *synthMutex;

void MainSynthWindow::toggleConnects (void)
{
	Gtk::MenuItem *me = (Gtk::MenuItem *)&menuJack.items()[0];
	Gtk::MenuItem *dis = (Gtk::MenuItem *)&menuJack.items()[1];
	bool c = me->is_sensitive();
	me->set_sensitive(!c);
	dis->set_sensitive(c);
}

#ifdef HAVE_JACK

static void connectDialog (int error)
{
	string msg;
	
	switch (error)
	{
		case gthJackAudio::ERR_NO_PLAYBACK:
			msg = "Could not find a playback target for JACK\n"
					"(alsa_pcm or oss.)";
			break;
		case gthJackAudio::ERR_HANDLE_NULL:
			msg = "Can't connect to the JACK server because it no\n"
			  		"longer seems to be running.";
			break;
		default:
			msg = "Could not (dis)connect JACK, errno = " + error;
			break;
	}

	Gtk::MessageDialog errorDialog (msg.c_str(), Gtk::MESSAGE_ERROR);
	errorDialog.run();
}

#endif /* HAVE_JACK */

MainSynthWindow::MainSynthWindow (thSynth *_synth, gthPrefs *_prefs, gthAudio *_audio)
{
	string ** vals;

	set_title("thinksynth");
	set_default_size(520, 360);

	synth = _synth;
	audio = _audio;
	prefs = _prefs;

	patchSel = new PatchSelWindow(synth);
	midiMap = NULL;
	prevDir = NULL;

	populateMenu();

	menuBar.accelerate(*patchSel);
//	menuBar.accelerate(*midiMap); /* this is created on-demand */

#ifdef HAVE_JACK
	/* Not the best place to do it but we need to call toggleConnects */
	if (dynamic_cast<gthJackAudio*>(audio) != NULL)
	{
		vals = prefs->Get("autoconnect");
		if (vals && *vals[0] == "true")
		{
			int error;
			if ((error = ((gthJackAudio*)audio)->tryConnect()) == 0)
				toggleConnects();
			else
				connectDialog (error);
		}
	}
#endif
	
	add(vbox);

	dspEntryLbl.set_label("DSP File: ");
	dspBrowseBtn.set_label("Browse");
	dspEntryBox.pack_start(dspEntryLbl, Gtk::PACK_SHRINK);
	dspEntryBox.pack_start(dspEntry, Gtk::PACK_EXPAND_WIDGET);
	dspEntryBox.pack_start(dspBrowseBtn, Gtk::PACK_SHRINK);

	dspEntry.signal_activate().connect(
		sigc::mem_fun(*this, &MainSynthWindow::onDspEntryActivate));

	dspBrowseBtn.signal_clicked().connect(
		sigc::mem_fun(*this, &MainSynthWindow::onBrowseButton));

	vbox.pack_start(menuBar, Gtk::PACK_SHRINK);
	vbox.pack_start(dspEntryBox, Gtk::PACK_SHRINK);
	vbox.pack_start(notebook, Gtk::PACK_EXPAND_WIDGET);

	notebook.set_scrollable();

	notebook.signal_switch_page().connect(
		sigc::mem_fun(*this, &MainSynthWindow::onSwitchPage));

	populate();

	show_all_children();

//	synth->signal_channel_changed().connect(
//		sigc::mem_fun(*this, &MainSynthWindow::channelChanged));
//	synth->signal_channel_deleted().connect(
//		sigc::mem_fun(*this, &MainSynthWindow::channelDeleted));
	gthPatchManager *patchMgr = gthPatchManager::instance();
	patchMgr->signal_patches_changed().connect(
		sigc::mem_fun(*this, &MainSynthWindow::onPatchesChanged));

	aboutBox = NULL;
}

MainSynthWindow::~MainSynthWindow (void)
{
	menuQuit();
}

void MainSynthWindow::populateMenu (void)
{
	/* File */
	{
		Gtk::Menu::MenuList &menulist = menuFile.items();

		menulist.push_back(
			Gtk::Menu_Helpers::MenuElem("_Keyboard",
										Gtk::AccelKey("<ctrl>k"),
										sigc::mem_fun(*this, &MainSynthWindow::menuKeyboard)));

		menulist.push_back(
			Gtk::Menu_Helpers::MenuElem("_Patch Selector",
										Gtk::AccelKey("<ctrl>p"),
										sigc::mem_fun(*this, &MainSynthWindow::menuPatchSel)));

		menulist.push_back(
			Gtk::Menu_Helpers::MenuElem("_MIDI Controllers",
										Gtk::AccelKey("<ctrl>m"),
										sigc::mem_fun(*this, &MainSynthWindow::menuMidiMap)));

		menulist.push_back(Gtk::Menu_Helpers::SeparatorElem());

		menulist.push_back(
			Gtk::Menu_Helpers::MenuElem("_Quit",
										Gtk::AccelKey("<ctrl>q"),
										sigc::mem_fun(*this, &MainSynthWindow::menuQuit)));
	}

#ifdef HAVE_JACK
	/* JACK */
	if (dynamic_cast<gthJackAudio*>(audio) != NULL)
	{
		Gtk::Menu::MenuList &menulist = menuJack.items();
		Gtk::CheckMenuItem *elem;
		sigc::slot0<void> autoslot =
			sigc::mem_fun(*this, &MainSynthWindow::menuJackAuto);
		string** vals;
		bool sel;
		
		menulist.push_back(
			Gtk::Menu_Helpers::MenuElem("_Connect to JACK now",
				sigc::mem_fun(*this, &MainSynthWindow::menuJackTry)));

		menulist.push_back(
			Gtk::Menu_Helpers::MenuElem("_Disconnect from JACK",
				sigc::mem_fun(*this, &MainSynthWindow::menuJackDis)));

		menulist.back().set_sensitive(false);

		menulist.push_back(Gtk::Menu_Helpers::SeparatorElem());

		menulist.push_back(
			Gtk::Menu_Helpers::CheckMenuElem ("_Auto-connect to JACK",
				autoslot));

		elem = (Gtk::CheckMenuItem*)&menulist.back();
	
		vals = prefs->Get("autoconnect");
		sel = !!(vals && *vals[0] == "true");
		elem->set_active(sel);
	}
#endif /* HAVE_JACK */
	
	/* Help */
	{
		Gtk::Menu::MenuList &menulist = menuHelp.items();

		menulist.push_back(
			Gtk::Menu_Helpers::MenuElem("_About",
										sigc::mem_fun(
											*this, &MainSynthWindow::menuAbout)
				));
	}

	/* add the menus to the menubar */
	{
		Gtk::Menu::MenuList &menulist = menuBar.items();

		menulist.push_back(Gtk::Menu_Helpers::MenuElem("_File",
													   menuFile));

#ifdef HAVE_JACK
		if (dynamic_cast<gthJackAudio*>(audio) != NULL)
			menulist.push_back(Gtk::Menu_Helpers::MenuElem("_JACK",
														menuJack));
#endif
		
		Gtk::MenuItem *helpMenu = manage(new Gtk::MenuItem("_Help", true));
		helpMenu->set_submenu(menuHelp);
		helpMenu->set_right_justified();
		menulist.push_back(*helpMenu);
	}
}

#ifdef HAVE_JACK

void MainSynthWindow::menuJackDis (void)
{
	gthJackAudio *jaudio = (gthJackAudio*)audio;
	
	if (jaudio)
	{
		jaudio->tryConnect(false);
		toggleConnects();
	}
}

void MainSynthWindow::menuJackTry (void)
{
	gthJackAudio *jaudio = (gthJackAudio*)audio;

	if (jaudio)
	{
		int error;
		if ((error = jaudio->tryConnect()) == 0)
			toggleConnects();
		else
			connectDialog(error);
	}
}

void MainSynthWindow::menuJackAuto (void)
{
	string *val = new string;
	string **vals = new string*[2];
	
	string **res = prefs->Get("autoconnect");

	vals[0] = val;
	vals[1] = NULL;

	if (res && *res[0] == "true")
	{
		if (chosen == false) { chosen = true; return; }
		*val = "false";
	}
	else
	{
		chosen = true;
		*val = "true";
	}
	
	debug("setting autoconnect to %s (was %s)", val->c_str(), res[0]->c_str());
	prefs->Set("autoconnect", vals);
}

#endif /* HAVE_JACK */

void MainSynthWindow::menuKeyboard (void)
{
	KeyboardWindow *kbwin = new KeyboardWindow (synth);
	/* menuBar.accelerate(*kbwin); */
	kbwin->show_all_children();
	kbwin->show();
	kbwin->signal_hide().connect(
		sigc::bind<KeyboardWindow *>(
			sigc::mem_fun(*this, &MainSynthWindow::onKeyboardHide), kbwin));
}

void MainSynthWindow::menuPatchSel (void)
{
	patchSel->show_all_children();
	patchSel->show();
}

void MainSynthWindow::menuMidiMap (void)
{
	if(!midiMap)
		midiMap = new MidiMap(synth);
	midiMap->show_all_children();
	midiMap->show();
}

void MainSynthWindow::menuQuit (void)
{
    hide();
}

void MainSynthWindow::menuAbout (void)
{
	if (aboutBox)
		return;

	aboutBox = new AboutBox;
	aboutBox->show();
	aboutBox->signal_hide().connect(
		sigc::mem_fun(*this, &MainSynthWindow::onAboutBoxHide));
}

void MainSynthWindow::append_tab (const string &tabName, int num, bool is_real)
{
	if (is_real == false)
	{
		Gtk::Label *lbl = manage(new Gtk::Label("Please select a DSP file to associate with this patch."));
		lbl->set_justify(Gtk::JUSTIFY_CENTER);
		notebook.append_page(*lbl, tabName);
		return;
	}

	gthPatchManager *patchMgr = gthPatchManager::instance();
	thMidiChan::ArgMap args = patchMgr->getChannelArgs(num);

	/* XXX: this no longer applies */
	/* only 'amp' */
	if (args.size() == 1)
	{
		Gtk::Label *sorry = manage(new Gtk::Label("Sorry, this DSP does not have modifiable settings."));
		sorry->set_justify(Gtk::JUSTIFY_CENTER);
		notebook.append_page(*sorry, tabName);
		return;
	}
		
	Gtk::ScrolledWindow *tab_view = manage(new Gtk::ScrolledWindow);
	Gtk::VBox *tab_vbox = manage(new Gtk::VBox);
	Gtk::Frame *info_frame = manage(new Gtk::Frame);
	Gtk::Table *info_table = manage(new Gtk::Table(3, 2));

	tab_view->add(*tab_vbox);
	tab_view->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);

	info_frame->set_label("DSP Information");
	info_frame->add(*info_table);

	info_table->set_col_spacings(5);
	info_table->set_row_spacings(5);

	thArg *dspName = args["name"];

	if (dspName)
	{
		Gtk::Label *lname_lbl = manage(new Gtk::Label("Name: "));
		Gtk::Label *rname_lbl = manage(new Gtk::Label(dspName->getComment()));

		lname_lbl->set_alignment(Gtk::ALIGN_RIGHT);
		rname_lbl->set_alignment(Gtk::ALIGN_LEFT);

		info_table->attach(*lname_lbl, 0, 1, 0, 1, Gtk::FILL, Gtk::FILL);
		info_table->attach(*rname_lbl, 1, 2, 0, 1, Gtk::FILL, Gtk::FILL);
	}

	thArg *dspAuthor = args["author"];

	if (dspAuthor)
	{
		Gtk::Label *lname_lbl = manage(new Gtk::Label("Author: "));
		Gtk::Label *rname_lbl = manage(new Gtk::Label(dspAuthor->getComment()));

		lname_lbl->set_alignment(Gtk::ALIGN_RIGHT);
		rname_lbl->set_alignment(Gtk::ALIGN_LEFT);

		
		info_table->attach(*lname_lbl, 0, 1, 1, 2, Gtk::FILL, Gtk::FILL);
		info_table->attach(*rname_lbl, 1, 2, 1, 2, Gtk::FILL, Gtk::FILL);
	}

	thArg *dspDesc = args["desc"];

	if (dspDesc)
	{
		Gtk::Label *lname_lbl = manage(new Gtk::Label("Description: "));
		Gtk::Label *rname_lbl = manage(new Gtk::Label(dspDesc->getComment()));

		lname_lbl->set_alignment(Gtk::ALIGN_RIGHT);
		rname_lbl->set_alignment(Gtk::ALIGN_LEFT);
		
		info_table->attach(*lname_lbl, 0, 1, 2, 3, Gtk::FILL, Gtk::FILL);
		info_table->attach(*rname_lbl, 1, 2, 2, 3, Gtk::FILL, Gtk::FILL);
	}

	Gtk::Frame *dsp_frame = manage(new Gtk::Frame);
	ArgTable *dsp_table = manage(new ArgTable);

	dsp_frame->set_label("DSP Parameters");
	dsp_frame->add(*dsp_table);
		
	tab_vbox->pack_start(*info_frame, Gtk::PACK_SHRINK);
	tab_vbox->pack_start(*dsp_frame);

	/* populate each tab */
	for (thMidiChan::ArgMap::iterator j = args.begin();
		 j != args.end(); j++)
	{
		string argName = j->first;
		thArg *arg = j->second;

		if (arg == NULL)
			continue;

		switch (arg->getWidgetType())
		{
			case thArg::HIDE:
				break;
			case thArg::SLIDER:
			{
				dsp_table->insertArg(arg);
				break;				
			}
			default:
				break;
		}
	}

	notebook.append_page(*tab_view, tabName);

}

void MainSynthWindow::populate (void)
{
	/* populate notebook */
	gthPatchManager *patchMgr = gthPatchManager::instance();
	int numPatches = patchMgr->numPatches();

	for (int i = 0; i < numPatches; i++)
	{
		gthPatchManager::PatchFile *patch = patchMgr->getPatch(i);

		if (patch == NULL)
		{
			/* XXX: BAD */
			string tabName = g_strdup_printf("%d: (Untitled)", i+1);
			append_tab (tabName, i, false);

			continue;
		}

		string tabName;
		if (patch->filename.length() > 0)
		{
			tabName = patch->filename;

			/* display channel # */
			tabName = g_strdup_printf("%d: %s", i + 1,
									  basename((char *)tabName.c_str()));
		}
		else
		{
			tabName = g_strdup_printf("%d: (Untitled)", i+1);
		}

		append_tab (tabName, i, true);
	}

}

void MainSynthWindow::onPatchesChanged (void)
{
	notebook.hide_all();
	notebook.pages().clear();
	populate();
	notebook.show_all();	
}

void MainSynthWindow::onAboutBoxHide (void)
{
	delete aboutBox;
	aboutBox = NULL;
}

void MainSynthWindow::onKeyboardHide (KeyboardWindow *kbwin)
{
	delete kbwin;
}

void MainSynthWindow::onSwitchPage (GtkNotebookPage *p, int pagenum)
{
	gthPatchManager *patchMgr = gthPatchManager::instance();
	gthPatchManager::PatchFile *patch = patchMgr->getPatch(pagenum);

	if (patch == NULL)
	{
		dspEntry.set_text("");
		return;
	}

	dspEntry.set_text(patch->dspFile);
}

void MainSynthWindow::onDspEntryActivate (void)
{
	gthPatchManager *patchMgr = gthPatchManager::instance();
	string dspfile = dspEntry.get_text();
	int pagenum = notebook.get_current_page();

	
	if (patchMgr->newPatch(dspfile, pagenum) == false)
	{
		printf("ERROR! Could not load DSP... [Pop-up a dialog]\n");
		return;
	}

	notebook.hide_all();
	notebook.pages().clear();
	populate();
	notebook.show_all();
}

void MainSynthWindow::onBrowseButton (void)
{
	gthPatchManager *patchMgr = gthPatchManager::instance();
	int pagenum = notebook.get_current_page();
	Gtk::FileSelection fileSel("thinksynth - Load Patch");

	if (prevDir)
		fileSel.set_filename(prevDir);

	if(fileSel.run() == Gtk::RESPONSE_OK)
	{
		dspEntry.set_text(fileSel.get_filename());

		if(patchMgr->newPatch(fileSel.get_filename(), pagenum))
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

			/* load up the patch file */
			notebook.hide_all();
			notebook.pages().clear();
			populate();
			notebook.show_all();
		}
		else
		{
			printf("ERROR! Could not load DSP... [Pop-up a dialog]\n");
		}
	}
}
