/* $Id: MainSynthWindow.cpp,v 1.1 2004/03/27 03:28:52 misha Exp $ */

#include "config.h"
#include "think.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <signal.h>

#include <gtkmm.h>
#include <gtkmm/messagedialog.h>

#include "thArg.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thMod.h"
#include "thMidiNote.h"
#include "thMidiChan.h"
#include "thSynth.h"

#include "PatchSelWindow.h"
#include "MainSynthWindow.h"

extern Glib::Mutex *synthMutex;

MainSynthWindow::MainSynthWindow (thSynth *synth)
	: patchSel (synth)
{
	set_title("thinksynth");
	set_default_size(320, 240);

	realSynth = synth;

	/* File */
	{
		Gtk::Menu::MenuList &menulist = menuFile.items();

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
			Gtk::Menu_Helpers::MenuElem("About"));
		
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

	vbox.pack_start(menuBar, Gtk::PACK_SHRINK);

	show_all_children();
}

MainSynthWindow::~MainSynthWindow (void)
{
	menuQuit();
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
