/* $Id: MidiMap.cpp,v 1.1 2004/10/28 01:21:47 ink Exp $ */
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
	set_size_request (360, 260);
	set_title("MIDI Controller Routing");

	realize();

	fixed = manage(new Gtk::Fixed);
	add(*fixed);

	btnClose = manage(new Gtk::Button("Close"));
	btnClose->signal_clicked().connect(sigc::mem_fun(*this, &MidiMap::onCloseButton));
	fixed->put(*btnClose, 5, 5);
	btnClose->set_size_request(88, 36);
	btnClose->set_flags(Gtk::CAN_DEFAULT);
	btnClose->grab_focus();
	btnClose->grab_default();

	notebook = manage(new Gtk::Notebook);
	fixed->put(*notebook, 8, 8);
	notebook->set_size_request(300, 200);

	show_all_children();
}

MidiMap::~MidiMap (void)
{
}

void MidiMap::onCloseButton (void)
{
	hide ();
}
