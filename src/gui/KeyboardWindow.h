/* $Id: KeyboardWindow.h,v 1.5 2004/04/01 07:46:26 misha Exp $ */

#ifndef KEYBOARD_WINDOW_H
#define KEYBOARD_WINDOW_H

class KeyboardWindow : public Gtk::Window
{
public:
	KeyboardWindow (thSynth *argsynth);
	~KeyboardWindow (void);

protected:
	bool clickEvent (GdkEventButton *b);
	bool keyEvent (GdkEventKey *k);
	bool exposeEvent (GdkEventExpose *e);
	void drawKeyboard (int mode);

	Gtk::DrawingArea drawArea;
	Gtk::VBox vbox;
	Gtk::Frame ctrlFrame;
	Gtk::Table ctrlTable;

	Gtk::Label chanLbl;
	Gtk::SpinButton *chanBtn;
	Gtk::Adjustment *chanVal;

	thSynth *synth;
private:
	GdkWindow *drawable;
	GdkGC		*kbgc;

	int img_width, img_height;
	int prv_active_keys[128];
	int active_keys[128];
	
	int ctrl_on;
	int shift_on;
	int alt_on;

	int mouse_notnum;
	int transpose;
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
