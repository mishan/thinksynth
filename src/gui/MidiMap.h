/* $Id: MidiMap.h,v 1.2 2004/11/09 00:23:45 ink Exp $ */
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

#ifndef MIDI_MAP_H
#define MIDI_MAP_H

class MidiMap : public Gtk::Window
{
public:
	MidiMap (thSynth *);
	~MidiMap (void);

protected:
	void onCloseButton (void);
	Gtk::VBox *main_vbox;
	Gtk::Button *close_btn;
};

#endif /* ABOUT_BOX_H */
