#include "config.h"

#include <gtkmm.h>

#include "think.h"

#include "PatchSelWindow.h"

PatchSelWindow::PatchSelWindow (void)
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
