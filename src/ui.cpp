/* $Id: ui.cpp,v 1.13 2004/06/30 03:47:45 misha Exp $ */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include <gtkmm.h>

#include "think.h"

#include "gui/Keyboard.h"
#include "gui/KeyboardWindow.h"
#include "gui/PatchSelWindow.h"
#include "gui/MainSynthWindow.h"

extern thSynth *Synth;
extern Gtk::Main *gtkMain;

void ui_thread (void)
{
	MainSynthWindow synthWindow(Synth);

	gtkMain->run(synthWindow);
}
