/* $Id: KeyboardWindow.h,v 1.9 2004/04/02 00:38:41 misha Exp $ */

#ifndef KEYBOARD_WINDOW_H
#define KEYBOARD_WINDOW_H

class KeyboardWindow : public Gtk::Window
{
public:
	KeyboardWindow (thSynth *argsynth);
	~KeyboardWindow (void);

protected:
	void drawKeyboard (int mode);
	void drawKeyboardFocus (void);

	/* event callbacks */
	bool focusInEvent (GdkEventFocus *f);
	bool focusOutEvent (GdkEventFocus *f);
	bool clickEvent (GdkEventButton *b);
	bool unclickEvent (GdkEventButton *b);
	bool keyEvent (GdkEventKey *k);
	bool exposeEvent (GdkEventExpose *e);

	thSynth *synth;
private:
	/* widgets */
	Gtk::DrawingArea drawArea;
	Gtk::VBox vbox;
	Gtk::Frame ctrlFrame;
	Gtk::Table ctrlTable;

	Gtk::Label chanLbl;
	Gtk::SpinButton *chanBtn;
	Gtk::Adjustment *chanVal;

	Gtk::Label transLbl;
	Gtk::SpinButton *transBtn;
	Gtk::Adjustment *transVal;

	/* lower-level widget stuff */
	GdkWindow *drawable;
	GdkGC *kbgc;
	bool focus_box;

	/* keyboard stuff */
	int img_width, img_height;
	int prv_active_keys[128];
	int active_keys[128];
	
	int ctrl_on;
	int shift_on;
	int alt_on;

	int mouse_notnum;
	int key_ofs;
	int veloc0;
	int veloc1;
	int veloc2;
	int veloc3;
	int mouse_veloc;

	int cur_size;

	int get_coord ();
	int keyval_to_notnum (int key);
};

#endif /* KEYBOARD_WINDOW_H */
