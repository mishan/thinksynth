/* $Id: MainSynthWindow.cpp,v 1.12 2004/08/01 11:05:18 misha Exp $ */

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

MainSynthWindow::MainSynthWindow (thSynth *synth)
	: patchSel (synth)
{
	set_title("thinksynth");
	set_default_size(520, 360);

	realSynth = synth;

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

	add(vbox);

	notebook.set_scrollable();

	vbox.pack_start(menuBar, Gtk::PACK_SHRINK);
	vbox.pack_start(notebook, Gtk::PACK_SHRINK);

//	menuBar.accelerate(keyboardWin);
	menuBar.accelerate(patchSel);

	/* populate notebook */
	int chans = realSynth->GetChannelCount();
	std::map<int, string> *patchList = realSynth->GetPatchlist();
	for (std::map<int, string>::iterator i = patchList->begin();
		 i != patchList->end(); i++)
	{
		string tabName = i->second;

		if (tabName.length() == 0)
			continue;

		std::map<string, thArg *> args = realSynth->GetChanArgs(i->first);

		Gtk::Table *table = new Gtk::Table(args.size(), 2);

		tabName = basename(tabName.c_str());
		int row = 0;

		/* populate each tab */
		for (std::map<string, thArg *>::iterator j = args.begin();
			 j != args.end(); j++)
		{
			string argName = j->first;
			thArg *arg = j->second;

			if (arg->argWidget != thArg::HIDE)
			{
				Gtk::Label *label = new Gtk::Label(argName);
//				ArgSlider *slider = new ArgSlider(arg);
				Gtk::HScale *slider = new Gtk::HScale(arg->argMin, arg->argMax, .01);

				slider->signal_value_changed().connect(
					SigC::bind<Gtk::HScale *, thArg *>(SigC::slot(*this, &MainSynthWindow::sliderChanged), slider, arg));
				slider->set_value(arg->argValues[0]);

				table->attach(*label, 0, 1, row, row+1);
				table->attach(*slider, 1, 2, row, row+1, Gtk::EXPAND|Gtk::FILL,
							  Gtk::EXPAND|Gtk::FILL); 
				row++;
			}
		}

		notebook.prepend_page(*table, tabName);
	}

	show_all_children();
}

MainSynthWindow::~MainSynthWindow (void)
{
	menuQuit();
}

void MainSynthWindow::sliderChanged (Gtk::HScale *slider, thArg *arg)
{
	arg->argValues[0] = slider->get_value();
}

void MainSynthWindow::menuKeyboard (void)
{
//	keyboardWin.show_all_children();
//	keyboardWin.show();
	KeyboardWindow *kbwin = new KeyboardWindow (realSynth);
	menuBar.accelerate(*kbwin);
	kbwin->show_all_children();
	kbwin->show();
}

void MainSynthWindow::menuPatchSel (void)
{
	patchSel.show_all_children();
	patchSel.show();
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
