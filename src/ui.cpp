/* $Id: ui.cpp,v 1.3 2004/03/24 10:05:20 misha Exp $ */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include <gtkmm.h>

#include "think.h"

#include "PatchSelWindow.h"

void ui_thread (void)
{
	Gtk::Main kit(NULL, NULL);

	PatchSelWindow patchsel;

	Gtk::Main::run(patchsel);
}
