/* $Id: MainSynthWindow.cpp,v 1.21 2004/09/05 00:21:48 misha Exp $ */
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

extern Glib::Mutex *synthMutex;

MainSynthWindow::MainSynthWindow (thSynth *_synth)
{
	set_title("thinksynth");
	set_default_size(520, 360);

	synth = _synth;

	patchSel = new PatchSelWindow(synth);

	populateMenu();

	menuBar.accelerate(*patchSel);

	add(vbox);

	vbox.pack_start(menuBar, Gtk::PACK_SHRINK);
	vbox.pack_start(notebook, Gtk::PACK_SHRINK);

	notebook.set_scrollable();

	populate();

	show_all_children();

	synth->signal_channel_changed().connect(
		SigC::slot(*this, &MainSynthWindow::channelChanged));
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

		menulist.push_back(Gtk::Menu_Helpers::MenuElem("_Help",
													   menuHelp));
	}
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
	kill (0, SIGTERM);
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

		printf("populating %s\n", tabName.c_str());

		tabName = basename(tabName.c_str());

		std::map<string, thArg *> args = synth->GetChanArgs(i->first);
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
	notebook.pages().clear();
	populate();
	notebook.show_all();
}
