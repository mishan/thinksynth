/* $Id: KeyboardWindow.h,v 1.15 2004/04/07 08:50:31 misha Exp $ */

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
};

#endif /* KEYBOARD_WINDOW_H */
