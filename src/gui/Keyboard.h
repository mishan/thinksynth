/* $Id: Keyboard.h,v 1.1 2004/04/02 11:33:11 misha Exp $ */

#ifndef KEYBOARD_H
#define KEYBOARD_H

class Keyboard : public Gtk::DrawingArea
{
public:
	Keyboard (Gtk::Container *parent, thSynth *argsynth);
	Keyboard (thSynth *argsynth, int argchan);
	~Keyboard ();

	void SetChannel (int argchan);
	void SetTranspose (int argtranspose);
protected:
	void drawKeyboard (int mode);
	void drawKeyboardFocus (void);

	/* event callbacks */
	bool exposeEvent (GdkEventExpose *e);
	bool focusInEvent (GdkEventFocus *f);
	bool focusOutEvent (GdkEventFocus *f);
	bool clickEvent (GdkEventButton *b);
	bool unclickEvent (GdkEventButton *b);
	bool keyEvent (GdkEventKey *k);

	thSynth *synth;
	int channel;
	int transpose;
private:
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

#endif /* KEYBOARD_H */
