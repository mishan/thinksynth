/* $Id: KeyboardWindow.h,v 1.17 2004/09/19 02:53:28 joshk Exp $ */
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

#ifndef KEYBOARD_WINDOW_H
#define KEYBOARD_WINDOW_H

class KeyboardWindow : public Gtk::Window
{
public:
	KeyboardWindow (thSynth *argsynth);
	~KeyboardWindow (void);

protected:
	void eventNoteOn (int chan, int note, float veloc);
	void eventNoteOff (int chan, int note);
	void eventChannelChanged (int chan);
	void eventTransposeChanged (int trans);

	void synthEventNoteOn (int chan, float note, float veloc);
	void synthEventNoteOff (int chan, float note);

	void changeChannel (void);
	void changeTranspose (void);
	void keyboardReset (void);

	virtual bool on_scroll_event (GdkEventScroll *s);

	thSynth *synth;
private:
	Glib::Mutex kbMutex;

	Keyboard keyboard;

	/* widgets */
	Gtk::VBox vbox;
	Gtk::Frame ctrlFrame;
	Gtk::Table ctrlTable;

	Gtk::Label chanLbl;
	Gtk::SpinButton *chanBtn;
	Gtk::Adjustment *chanVal;

	Gtk::Label transLbl;
	Gtk::SpinButton *transBtn;
	Gtk::Adjustment *transVal;

	Gtk::Button resetBtn;
};

#endif /* KEYBOARD_WINDOW_H */
