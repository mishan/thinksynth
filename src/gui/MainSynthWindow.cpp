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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <signal.h>

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

bool chosen = false;

void MainSynthWindow::toggleConnects (void)
{
	Gtk::MenuItem *me = (Gtk::MenuItem *)&menuJack_.items()[0];
	Gtk::MenuItem *dis = (Gtk::MenuItem *)&menuJack_.items()[1];
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

	Gtk::MessageDialog errorDialog (msg.c_str(), false, Gtk::MESSAGE_ERROR);
	errorDialog.run();
}

#endif /* HAVE_JACK */

MainSynthWindow::MainSynthWindow (gthAudio *audio)
{
	gthPrefs *prefs = gthPrefs::instance();
	string **vals;

	audio_ = audio;

	set_title("thinksynth");
	set_default_size(520, 360);

	midiMap_ = NULL;
	patchSel_ = NULL;
	aboutBox_ = NULL;
	kbWin_ = NULL;

	vals = prefs->Get("dspdir");

	if (vals != NULL)
		prevDir_ = strdup(vals[0]->c_str());
	else
		prevDir_ = strdup(DSP_PATH);

	populateMenu();

#ifdef HAVE_JACK
	signal_realize().connect(
		sigc::mem_fun(*this, &MainSynthWindow::jackCheck));
#endif
	
	add(vbox_);

	dspEntryLbl_.set_label("DSP File: ");
	dspBrowseBtn_.set_label("Browse");
	dspEntryBox_.pack_start(dspEntryLbl_, Gtk::PACK_SHRINK);
	dspEntryBox_.pack_start(dspEntry_, Gtk::PACK_EXPAND_WIDGET);
	dspEntryBox_.pack_start(dspBrowseBtn_, Gtk::PACK_SHRINK);

	dspEntry_.signal_activate().connect(
		sigc::mem_fun(*this, &MainSynthWindow::onDspEntryActivate));

	dspBrowseBtn_.signal_clicked().connect(
		sigc::mem_fun(*this, &MainSynthWindow::onBrowseButton));

	vbox_.pack_start(menuBar_, Gtk::PACK_SHRINK);
	vbox_.pack_start(dspEntryBox_, Gtk::PACK_SHRINK);
	vbox_.pack_start(notebook_, Gtk::PACK_EXPAND_WIDGET);

	notebook_.set_scrollable();

	notebook_.signal_switch_page().connect(
		sigc::mem_fun(*this, &MainSynthWindow::onSwitchPage));

	populate();

	show_all_children();

	gthPatchManager *patchMgr = gthPatchManager::instance();
	patchMgr->signal_patches_changed().connect(
		sigc::mem_fun(*this, &MainSynthWindow::onPatchesChanged));
	patchMgr->signal_patch_load_error().connect(
		sigc::mem_fun(*this, &MainSynthWindow::onPatchLoadError));

	debug("signal connections made");

}

MainSynthWindow::~MainSynthWindow (void)
{
	menuQuit();
}

void MainSynthWindow::populateMenu (void)
{
	/* File */
	{
		Gtk::Menu::MenuList &menulist = menuFile_.items();

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
	if (dynamic_cast<gthJackAudio*>(audio_) != NULL)
	{
		gthPrefs *prefs = gthPrefs::instance();
		Gtk::Menu::MenuList &menulist = menuJack_.items();
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
		Gtk::Menu::MenuList &menulist = menuHelp_.items();

		menulist.push_back(
			Gtk::Menu_Helpers::MenuElem("_About",
										sigc::mem_fun(
											*this, &MainSynthWindow::menuAbout)
				));
	}

	/* add the menus to the menubar */
	{
		Gtk::Menu::MenuList &menulist = menuBar_.items();

		menulist.push_back(Gtk::Menu_Helpers::MenuElem("_File",
													   menuFile_));

#ifdef HAVE_JACK
		if (dynamic_cast<gthJackAudio*>(audio_) != NULL)
			menulist.push_back(Gtk::Menu_Helpers::MenuElem("_JACK",
														menuJack_));
#endif
		
		Gtk::MenuItem *helpMenu = manage(new Gtk::MenuItem("_Help", true));
		helpMenu->set_submenu(menuHelp_);
		helpMenu->set_right_justified();
		menulist.push_back(*helpMenu);
	}
}

#ifdef HAVE_JACK

void MainSynthWindow::menuJackDis (void)
{
	gthJackAudio *jaudio = (gthJackAudio*)audio_;
	
	if (jaudio)
	{
		jaudio->tryConnect(false);
		toggleConnects();
	}
}

void MainSynthWindow::menuJackTry (void)
{
	gthJackAudio *jaudio = (gthJackAudio*)audio_;

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
	gthPrefs *prefs = gthPrefs::instance();
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
	
	prefs->Set("autoconnect", vals);
}

#endif /* HAVE_JACK */

void MainSynthWindow::menuKeyboard (void)
{
	if (kbWin_ == NULL)
	{
		kbWin_ = new KeyboardWindow (thSynth::instance());
		menuBar_.accelerate(*kbWin_);
		kbWin_->signal_hide().connect(
			sigc::mem_fun(*this, &MainSynthWindow::onKeyboardHide));
	}

	kbWin_->show_all_children();
	kbWin_->show();
}

void MainSynthWindow::menuPatchSel (void)
{
	if (patchSel_ == NULL)
	{
		patchSel_ = new PatchSelWindow(thSynth::instance());
		menuBar_.accelerate(*patchSel_);
		patchSel_->signal_hide().connect(
			sigc::mem_fun(*this, &MainSynthWindow::onPatchSelHide));
	}
	
	patchSel_->show_all_children();
	patchSel_->show();
}

void MainSynthWindow::menuMidiMap (void)
{
	if (midiMap_ == NULL)
	{
		midiMap_ = new MidiMap(thSynth::instance());
		menuBar_.accelerate(*midiMap_);
		midiMap_->signal_hide().connect(
			sigc::mem_fun(*this, &MainSynthWindow::onMidiMapHide));
	}

	midiMap_->show_all_children();
	midiMap_->show();
}

void MainSynthWindow::menuQuit (void)
{
    hide();
}

void MainSynthWindow::menuAbout (void)
{
	if (aboutBox_)
		return;

	aboutBox_ = new AboutBox;
	aboutBox_->show();
	aboutBox_->signal_hide().connect(
		sigc::mem_fun(*this, &MainSynthWindow::onAboutBoxHide));
}

void MainSynthWindow::append_tab (const string &tabName, int num, bool is_real)
{
	if (is_real == false)
	{
		Gtk::Label *lbl = manage(new Gtk::Label("Please select a DSP file to associate with this patch."));
		lbl->set_justify(Gtk::JUSTIFY_CENTER);
		notebook_.append_page(*lbl, tabName);
		return;
	}

	gthPatchManager *patchMgr = gthPatchManager::instance();
	thArgMap args = patchMgr->getChannelArgs(num);

	/* XXX: this no longer applies */
	/* only 'amp' */
	if (args.size() == 1)
	{
		Gtk::Label *sorry = manage(new Gtk::Label("Sorry, this DSP does not have modifiable settings."));
		sorry->set_justify(Gtk::JUSTIFY_CENTER);
		notebook_.append_page(*sorry, tabName);
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
		Gtk::Label *rname_lbl = manage(new Gtk::Label(dspName->comment()));

		lname_lbl->set_alignment(Gtk::ALIGN_RIGHT);
		rname_lbl->set_alignment(Gtk::ALIGN_LEFT);

		info_table->attach(*lname_lbl, 0, 1, 0, 1, Gtk::FILL, Gtk::FILL);
		info_table->attach(*rname_lbl, 1, 2, 0, 1, Gtk::FILL, Gtk::FILL);
	}

	thArg *dspAuthor = args["author"];

	if (dspAuthor)
	{
		Gtk::Label *lname_lbl = manage(new Gtk::Label("Author: "));
		Gtk::Label *rname_lbl = manage(new Gtk::Label(dspAuthor->comment()));

		lname_lbl->set_alignment(Gtk::ALIGN_RIGHT);
		rname_lbl->set_alignment(Gtk::ALIGN_LEFT);

		
		info_table->attach(*lname_lbl, 0, 1, 1, 2, Gtk::FILL, Gtk::FILL);
		info_table->attach(*rname_lbl, 1, 2, 1, 2, Gtk::FILL, Gtk::FILL);
	}

	thArg *dspDesc = args["desc"];

	if (dspDesc)
	{
		Gtk::Label *lname_lbl = manage(new Gtk::Label("Description: "));
		Gtk::Label *rname_lbl = manage(new Gtk::Label(dspDesc->comment()));

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
	for (thArgMap::iterator j = args.begin();
		 j != args.end(); j++)
	{
		string argName = j->first;
		thArg *arg = j->second;

		if (arg == NULL)
			continue;

		switch (arg->widgetType())
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

	notebook_.append_page(*tab_view, tabName);

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
									  thUtil::basename((char*)tabName.c_str()));
		}
		else
		{
			tabName = g_strdup_printf("%d: (Untitled)", i+1);
		}

		append_tab (tabName, i, true);
	}

}

#ifdef HAVE_JACK
void MainSynthWindow::jackCheck (void)
{
	gthPrefs *prefs = gthPrefs::instance();
	string ** vals;

	/* Not the best place to do it but we need to call toggleConnects */
	if (dynamic_cast<gthJackAudio*>(audio_) != NULL)
	{
		vals = prefs->Get("autoconnect");
		if (vals && *vals[0] == "true")
		{
			int error;
			if ((error = ((gthJackAudio*)audio_)->tryConnect()) == 0)
				toggleConnects();
			else
				connectDialog (error);
		}
	}
}
#endif

void MainSynthWindow::onPatchesChanged (void)
{
	int pagenum = notebook_.get_current_page();

	notebook_.hide_all();
	notebook_.pages().clear();
	populate();
	notebook_.show_all();

	if (pagenum != -1)
		notebook_.set_current_page(pagenum);
}

void MainSynthWindow::onPatchLoadError (const char* failure)
{
	char *error = g_strdup_printf("Couldn't load patchfile %s; syntax error, or DSP does not exist",
		failure);
		
	Gtk::MessageDialog errorDialog (error, false, Gtk::MESSAGE_ERROR);
	
	errorDialog.run();
	free(error);
}

void MainSynthWindow::onAboutBoxHide (void)
{
	delete aboutBox_;
	aboutBox_ = NULL;
}

void MainSynthWindow::onPatchSelHide (void)
{
	delete patchSel_;
	patchSel_ = NULL;
}

void MainSynthWindow::onMidiMapHide (void)
{
	delete midiMap_;
	midiMap_ = NULL;
}

void MainSynthWindow::onKeyboardHide (void)
{
	delete kbWin_;
	kbWin_ = NULL;
}

void MainSynthWindow::onSwitchPage (GtkNotebookPage *p, int pagenum)
{
	gthPatchManager *patchMgr = gthPatchManager::instance();
	gthPatchManager::PatchFile *patch = patchMgr->getPatch(pagenum);

	if (patch == NULL)
	{
		dspEntry_.set_text("");
		return;
	}

	dspEntry_.set_text(patch->dspFile);
}

void MainSynthWindow::onDspEntryActivate (void)
{
	gthPatchManager *patchMgr = gthPatchManager::instance();
	string dspfile = dspEntry_.get_text();
	int pagenum = notebook_.get_current_page();

	/* noop caused by a spurious Enter */
	if (dspfile == "")
		return;
	
	if (patchMgr->newPatch(dspfile, pagenum) == false)
	{
		char *error = g_strdup_printf("Couldn't load DSP %s; syntax error, or does not exist",
			dspfile.c_str());
		
		Gtk::MessageDialog errorDialog (error, false, Gtk::MESSAGE_ERROR);
		
		errorDialog.run();

		free(error);

		return;
	}

	notebook_.hide_all();
	notebook_.pages().clear();
	populate();
	notebook_.show_all();

	notebook_.set_current_page(pagenum);
}

void MainSynthWindow::onBrowseButton (void)
{
	gthPatchManager *patchMgr = gthPatchManager::instance();
	int pagenum = notebook_.get_current_page();
	Gtk::FileSelection fileSel("thinksynth - Load DSP");

	if (prevDir_)
		fileSel.set_filename(prevDir_);

	if (fileSel.run() == Gtk::RESPONSE_OK)
	{
		dspEntry_.set_text(fileSel.get_filename());

		if (patchMgr->newPatch(fileSel.get_filename(), pagenum))
		{
			char *dn = thUtil::dirname((char*)fileSel.get_filename().c_str());

			if (prevDir_)
				free (prevDir_);

			prevDir_ = g_strdup_printf("%s/", dn);
			free (dn);

			string **vals = new string *[2];
			vals[0] = new string(prevDir_);
			vals[1] = NULL;

			gthPrefs *prefs = gthPrefs::instance();
			prefs->Set("dspdir", vals);

			/* load up the patch file */
			notebook_.hide_all();
			notebook_.pages().clear();
			populate();
			notebook_.show_all();
			notebook_.set_current_page(pagenum);
		}
		else
		{
			char *error = g_strdup_printf("Couldn't load DSP %s; syntax error, or does not exist",
				fileSel.get_filename().c_str());
		
			Gtk::MessageDialog errorDialog (error, false, Gtk::MESSAGE_ERROR);
		
			errorDialog.run();

			free(error);
		}
	}
}
