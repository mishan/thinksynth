/* $Id: ui.cpp,v 1.16 2004/09/16 07:59:06 misha Exp $ */
/*
 * Copyright (C) 2004 Metaphonic Labs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include <gtkmm.h>

#include "think.h"

#include "gthAudio.h"
#include "gthPrefs.h"

#include "gui/Keyboard.h"
#include "gui/KeyboardWindow.h"
#include "gui/PatchSelWindow.h"
#include "gui/MainSynthWindow.h"

extern thSynth *Synth;
extern gthPrefs *prefs;
extern gthAudio *aout;

void ui_thread (void)
{
}
