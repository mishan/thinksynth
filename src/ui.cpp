/* $Id: ui.cpp,v 1.12 2004/05/25 03:54:04 misha Exp $ */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include <gtkmm.h>

#include "think.h"

#include "Keyboard.h"
#include "KeyboardWindow.h"
#include "PatchSelWindow.h"
#include "MainSynthWindow.h"

extern thSynth *Synth;
extern Gtk::Main *gtkMain;

void ui_thread (void)
{
	MainSynthWindow synthWindow(Synth);

	gtkMain->run(synthWindow);
}
