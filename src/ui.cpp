/* $Id: ui.cpp,v 1.2 2004/03/24 10:01:06 misha Exp $ */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include <gtkmm.h>

#include "think.h"

class PatchSelWindow : public Gtk::Window
{
public:
	inline PatchSelWindow (void)
	{
//		set_default_size(320, 240);

		set_title("thinksynth - Patch Selector");

		add(vbox);

		vbox.pack_start(patchTable, Gtk::PACK_SHRINK);

		for(int i = 0; i <= 6; i++)
		{
			char label[11];

			sprintf(label, "Channel %d", i+1);
			Gtk::Label *chanLabel = new Gtk::Label(label);
			Gtk::Entry *chanEntry = new Gtk::Entry;

			patchTable.attach(*chanLabel, 0, 1, i, i+1);
			patchTable.attach(*chanEntry, 1, 2, i, i+1);
		}


		show_all_children();
	}
protected:
	Gtk::VBox vbox;

	Gtk::Table patchTable;
	
private:
};

void ui_thread (void)
{
	Gtk::Main kit(NULL, NULL);

	PatchSelWindow patchsel;

	Gtk::Main::run(patchsel);
}
