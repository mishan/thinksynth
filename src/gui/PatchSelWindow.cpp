#include "config.h"
#include "think.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtkmm.h>

#include "thArg.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thMod.h"
#include "thMidiNote.h"
#include "thMidiChan.h"
#include "thSynth.h"

#include "PatchSelWindow.h"

extern Glib::Mutex *synthMutex;
extern thSynth Synth;

PatchSelWindow::PatchSelWindow (thSynth *synth)
{
//		set_default_size(320, 240);

	realSynth = synth;
	mySynth = new thSynth(realSynth);

	set_title("thinksynth - Patch Selector");
	
	add(vbox);
	
	vbox.pack_start(patchTable, Gtk::PACK_SHRINK);

	synthMutex->lock();
		
	std::map<int, string> *patchlist = Synth.GetPatchlist();
	int channelcount = Synth.GetChannelCount();

	synthMutex->unlock();

	for(int i = 0; i < channelcount; i++)
	{
		char label[11];

//		thMidiChan *chan = synth->GetChannel(i);
//		thMod *mod = chan->GetMod();

		sprintf(label, "Channel %d", i+1);
		Gtk::Label *chanLabel = new Gtk::Label(label);
		string filename = (*patchlist)[i];
		Gtk::Entry *chanEntry = new Gtk::Entry;
		chanEntry->set_text(Glib::ustring (filename.c_str()));

		patchTable.attach(*chanLabel, 0, 1, i, i+1);
		patchTable.attach(*chanEntry, 1, 2, i, i+1);
	}

	show_all_children();
}
