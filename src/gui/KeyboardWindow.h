/* $Id$ */
/*
 * Copyright (C) 2004-2005 Metaphonic Labs
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
	KeyboardWindow (thSynth *synth);
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
	void keyboardResetKeys (void);

	virtual bool on_scroll_event (GdkEventScroll *s);

	thSynth *synth_;
private:
	Glib::Mutex kbMutex_;

	Keyboard *keyboard_;

	/* widgets */
	Gtk::VBox vbox_;
	Gtk::Frame *ctrlFrame_;
	Gtk::Table *ctrlTable_;

	Gtk::Label *chanLbl_;
	Gtk::SpinButton *chanBtn_;
	Gtk::Adjustment *chanVal_;

	Gtk::Label *transLbl_;
	Gtk::SpinButton *transBtn_;
	Gtk::Adjustment *transVal_;

	Gtk::Button *resetBtn_;
};

#endif /* KEYBOARD_WINDOW_H */
