/* $Id: ui.cpp,v 1.5 2004/03/26 06:29:35 misha Exp $ */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include <gtkmm.h>

#include "think.h"

#include "thArg.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thMod.h"
#include "thMidiNote.h"
#include "thMidiChan.h"
#include "thSynth.h"

#include "PatchSelWindow.h"

extern thSynth Synth;

void ui_thread (void)
{
	Gtk::Main kit(NULL, NULL);

	PatchSelWindow patchsel(&Synth);

	Gtk::Main::run(patchsel);
}
