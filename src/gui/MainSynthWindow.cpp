/* $Id: MainSynthWindow.cpp,v 1.29 2004/09/15 08:31:38 joshk Exp $ */
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

#include <gtkmm.h>
#include <gtkmm/messagedialog.h>

#include "think.h"

#include "PatchSelWindow.h"
#include "Keyboard.h"
#include "KeyboardWindow.h"
#include "MainSynthWindow.h"
#include "ArgSlider.h"

#include "../gthJackAudio.h"
#include "../gthPrefs.h"

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

MainSynthWindow::MainSynthWindow (thSynth *_synth, gthPrefs *_prefs, gthAudio *_audio)
{
	string ** vals;
	
	set_title("thinksynth");
	set_default_size(520, 360);

	synth = _synth;
	audio = _audio;
	prefs = _prefs;

	patchSel = new PatchSelWindow(synth);

	populateMenu();

	menuBar.accelerate(*patchSel);

	/* Not the best place to do it but we need to call toggleConnects */
	if (dynamic_cast<gthJackAudio*>(audio) != NULL)
	{
		vals = prefs->Get("autoconnect");
		if (*vals[0] == "true")
		{
			if (((gthJackAudio*)audio)->tryConnect())
				toggleConnects();
		}
	}
	
	add(vbox);

	vbox.pack_start(menuBar, Gtk::PACK_SHRINK);
	vbox.pack_start(notebook, Gtk::PACK_EXPAND_WIDGET);

	notebook.set_scrollable();

	populate();

	show_all_children();

	synth->signal_channel_changed().connect(
		SigC::slot(*this, &MainSynthWindow::channelChanged));

	synth->signal_channel_deleted().connect(
		SigC::slot(*this, &MainSynthWindow::channelDeleted));
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
										Gtk::Menu::AccelKey("<ctrl>k"),
										SigC::slot(*this, &MainSynthWindow::menuKeyboard)));

		menulist.push_back(
			Gtk::Menu_Helpers::MenuElem("_Patch Selector",
										Gtk::Menu::AccelKey("<ctrl>p"),
										SigC::slot(*this, &MainSynthWindow::menuPatchSel)));

		menulist.push_back(Gtk::Menu_Helpers::SeparatorElem());

		menulist.push_back(
			Gtk::Menu_Helpers::MenuElem("_Quit",
										Gtk::Menu::AccelKey("<ctrl>q"),
										SigC::slot(*this, &MainSynthWindow::menuQuit)));
	}

	/* JACK */
	if (dynamic_cast<gthJackAudio*>(audio) != NULL)
	{
		Gtk::Menu::MenuList &menulist = menuJack.items();
		Gtk::CheckMenuItem *elem;
		SigC::Slot0<void> autoslot = SigC::slot(*this, &MainSynthWindow::menuJackAuto);
		string** vals;
		bool sel;
		
		menulist.push_back(
			Gtk::Menu_Helpers::MenuElem("Connect to JACK now",
				SigC::slot(*this, &MainSynthWindow::menuJackTry)));

		menulist.push_back(
			Gtk::Menu_Helpers::MenuElem("Disconnect from JACK",
				SigC::slot(*this, &MainSynthWindow::menuJackDis)));

		menulist.back().set_sensitive(false);

		menulist.push_back(Gtk::Menu_Helpers::SeparatorElem());

		menulist.push_back(
			Gtk::Menu_Helpers::CheckMenuElem ("Auto-connect to JACK",
				autoslot));

		elem = (Gtk::CheckMenuItem*)&menulist.back();
	
		vals = prefs->Get("autoconnect");
		sel = !!(vals && *vals[0] == "true");
		debug("set to %s; about to set_active -> %d", vals[0]->c_str(), (int)sel);
		elem->set_active(sel);
		debug("done");
	}
	
	/* Help */
	{
		Gtk::Menu::MenuList &menulist = menuHelp.items();

		menulist.push_back(
			Gtk::Menu_Helpers::MenuElem("About",
										SigC::slot(
											*this, &MainSynthWindow::menuAbout)
				));
	}

	/* add the menus to the menubar */
	{
		Gtk::Menu::MenuList &menulist = menuBar.items();

		menulist.push_back(Gtk::Menu_Helpers::MenuElem("_File",
													   menuFile));

		if (dynamic_cast<gthJackAudio*>(audio) != NULL)
			menulist.push_back(Gtk::Menu_Helpers::MenuElem("_JACK",
														menuJack));

		menulist.push_back(Gtk::Menu_Helpers::MenuElem("_Help",
													   menuHelp));
	}
}

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
		if (jaudio->tryConnect())
			toggleConnects();
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

void MainSynthWindow::menuKeyboard (void)
{
	KeyboardWindow *kbwin = new KeyboardWindow (synth);
	menuBar.accelerate(*kbwin);
	kbwin->show_all_children();
	kbwin->show();
}

void MainSynthWindow::menuPatchSel (void)
{
	patchSel->show_all_children();
	patchSel->show();
}

void MainSynthWindow::menuQuit (void)
{
	kill (0, SIGUSR1);
}

void MainSynthWindow::menuAbout (void)
{
	Gtk::MessageDialog dialog (PACKAGE_STRING, Gtk::MESSAGE_INFO,
							   Gtk::BUTTONS_OK, true, true);

	dialog.run();
}

void MainSynthWindow::sliderChanged (Gtk::HScale *slider, thArg *arg)
{
	arg->argValues[0] = slider->get_value();
}

void MainSynthWindow::populate (void)
{
	/* populate notebook */
	std::map<int, string> *patchList = synth->GetPatchlist();

	for (std::map<int, string>::iterator i = patchList->begin();
		 i != patchList->end(); i++)
	{
		string tabName = i->second;

		if (tabName.length() == 0)
			continue;

		/* display channel # */
		tabName = g_strdup_printf("%d: %s", i->first + 1,
			basename(tabName.c_str()));

		std::map<string, thArg *> args = synth->GetChanArgs(i->first);
		
		
		/* only 'amp' */
		if (args.size() == 1)
		{
			Gtk::Label *sorry = manage(new Gtk::Label("Sorry, this DSP does not have modifiable settings."));
			sorry->set_justify(Gtk::JUSTIFY_CENTER);
			notebook.append_page(*sorry, tabName);
			continue;
		}
		
		Gtk::Table *table = manage(new Gtk::Table(args.size(), 3));
		int row = 0;

		/* populate each tab */
		for (std::map<string, thArg *>::iterator j = args.begin();
			 j != args.end(); j++)
		{
			string argName = j->first;
			thArg *arg = j->second;

			if (arg == NULL)
				continue;

			switch (arg->argWidget)
			{
				case thArg::HIDE:
					break;
				case thArg::SLIDER:
				{
					Gtk::Label *label = manage(new Gtk::Label(
											   arg->argLabel.length() > 0 ? 
											   arg->argLabel : argName));

					Gtk::HScale *slider = manage(new Gtk::HScale(arg->argMin,
																 arg->argMax,
																 .0001));
					Gtk::Adjustment *argAdjust = slider->get_adjustment();

					slider->set_draw_value(false);

					slider->signal_value_changed().connect(
						SigC::bind<Gtk::HScale *, thArg *>(
							SigC::slot(*this, &MainSynthWindow::sliderChanged),
							slider, arg));

					slider->set_value(arg->argValues[0]);

					Gtk::SpinButton *valEntry = manage(new Gtk::SpinButton(
														   *argAdjust, .0001,
														   4));

					table->attach(*label, 0, 1, row, row+1, Gtk::SHRINK,
								  Gtk::SHRINK);
					table->attach(*slider, 1, 2, row, row+1,
								  Gtk::EXPAND|Gtk::FILL,
								  Gtk::EXPAND|Gtk::FILL);
					table->attach(*valEntry, 2, 3, row, row+1,
								  Gtk::SHRINK|Gtk::FILL,
								  Gtk::SHRINK|Gtk::FILL);
					row++;

					break;				
				}
				default:
					break;
			}
		}

		notebook.append_page(*table, tabName);
	}

}

void MainSynthWindow::channelChanged (string filename, int chan, float amp)
{
	notebook.hide_all();
	notebook.pages().clear();
	populate();
	notebook.show_all();
}

void MainSynthWindow::channelDeleted (int chan)
{
	notebook.hide_all();
	notebook.pages().clear();
	populate();
	notebook.show_all();
}
