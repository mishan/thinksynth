/* $Id: ui.cpp,v 1.10 2004/04/06 04:07:36 misha Exp $ */

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

#include "Keyboard.h"
#include "KeyboardWindow.h"
#include "PatchSelWindow.h"
#include "MainSynthWindow.h"

extern thSynth Synth;
extern Gtk::Main *gtkMain;

void ui_thread (void)
{
	MainSynthWindow synthWindow(&Synth);

	gtkMain->run(synthWindow);
}
