/* $Id: PatchSelWindow.cpp,v 1.14 2004/03/27 04:12:18 joshk Exp $ */

#include "config.h"
#include "think.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

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

extern Glib::Mutex *synthMutex;

PatchSelWindow::PatchSelWindow (thSynth *synth)
{
//		set_default_size(320, 240);
	set_default_size(400, 0);

	realSynth = synth;
	mySynth = NULL;
//	mySynth = new thSynth(realSynth);

	set_title("thinksynth - Patch Selector");
	
	add(vbox);
	
	vbox.pack_start(patchTable, Gtk::PACK_SHRINK);

	synthMutex->lock();
		
	std::map<int, string> *patchlist = realSynth->GetPatchlist();
	int channelcount = realSynth->GetChannelCount();

	synthMutex->unlock();

	for(int i = 0; i < channelcount; i++)
	{
		char label[11];

		sprintf(label, "Channel %d", i+1);
		Gtk::Label *chanLabel = new Gtk::Label(label);
		string filename = (*patchlist)[i];
		Gtk::Entry *chanEntry = new Gtk::Entry;
		chanEntry->set_text(filename);

		int *channum = new int;
		*channum = i;

		Gtk::HScale *chanAmp = new Gtk::HScale(0, MIDIVALMAX, .5);
		chanAmp->set_data("channel", channum);
		chanAmp->signal_value_changed().connect(
			SigC::bind<Gtk::HScale *, thSynth *>
			(SigC::slot(*this, &PatchSelWindow::SetChannelAmp),
			 chanAmp, realSynth));

		thArg *amp = realSynth->GetChanArg(i, "amp");
		if (amp)
		{
			chanAmp->set_value(amp->argValues[0]);
		}
		else
		{
			chanAmp->set_sensitive(false);
		}

		patchTable.attach(*chanLabel, 0, 1, i, i+1, Gtk::FILL, Gtk::SHRINK);
		patchTable.attach(*chanEntry, 1, 2, i, i+1, Gtk::FILL, Gtk::SHRINK);
		patchTable.attach(*chanAmp, 2, 3, i, i+1, Gtk::FILL|Gtk::EXPAND,
						  Gtk::FILL);


		chanEntry->set_data("channel", channum);
		chanEntry->set_data("amp", chanAmp);
		chanEntry->signal_activate().connect(
			SigC::bind<Gtk::Entry *, thSynth *>
			(SigC::slot(*this, &PatchSelWindow::LoadPatch), 
			 chanEntry, realSynth));
	}
}

PatchSelWindow::~PatchSelWindow (void)
{
	hide ();
}

void PatchSelWindow::LoadPatch (Gtk::Entry *chanEntry, thSynth *synth)
{
	int *channum = (int *)chanEntry->get_data("channel");
	Gtk::HScale *chanAmp = (Gtk::HScale *)chanEntry->get_data("amp");
	thMod* loaded = NULL;

	if (chanEntry->get_text().empty())
	{
		return;
	}

	synthMutex->lock();

	if ((loaded = synth->LoadMod(chanEntry->get_text().c_str(), *channum,
		(float)chanAmp->get_value())) == NULL)
	{
		char *error = g_strdup_printf("Couldn't load %s: %s",
									chanEntry->get_text().c_str(),
									strerror(errno));
		Gtk::MessageDialog errorDialog (error, Gtk::MESSAGE_ERROR);

		errorDialog.run();
		g_free(error);
		chanAmp->set_sensitive(false);
	}
	else
	{
		chanAmp->set_sensitive(true);
	}
	
	synthMutex->unlock();
}

void PatchSelWindow::SetChannelAmp (Gtk::HScale *scale, thSynth *synth)
{
	int *channum = (int *)scale->get_data("channel");
	float *val = new float;
	*val = (float)scale->get_value();
	thArg *arg = new thArg("amp", val, 1);

	synthMutex->lock();

	synth->SetChanArg(*channum, arg);

	synthMutex->unlock();
}
