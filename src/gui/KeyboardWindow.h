/* $Id: KeyboardWindow.h,v 1.11 2004/04/03 02:18:43 misha Exp $ */

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

	void changeChannel (void);
	void changeTranspose (void);

	thSynth *synth;
private:
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
};

#endif /* KEYBOARD_WINDOW_H */
