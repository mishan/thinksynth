/* $Id: MidiMap.cpp,v 1.2 2004/11/09 00:23:45 ink Exp $ */
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

#include <gtkmm.h>

#include "think.h"
#include "MidiMap.h"

MidiMap::MidiMap (thSynth *argsynth)
{
	set_title("MIDI Controller Routing");

	main_vbox = manage(new Gtk::VBox);
	close_btn = manage(new Gtk::Button("Close Window"));
	close_btn->signal_clicked().connect(sigc::mem_fun(*this, &MidiMap::onCloseButton));

	add(*main_vbox);
	main_vbox->pack_start(*close_btn, Gtk::PACK_EXPAND_WIDGET);


	show_all_children();
}

MidiMap::~MidiMap (void)
{
}

void MidiMap::onCloseButton (void)
{
	hide();
}
