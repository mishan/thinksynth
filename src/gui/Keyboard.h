/* $Id: Keyboard.h,v 1.2 2004/04/03 02:18:43 misha Exp $ */

#ifndef KEYBOARD_H
#define KEYBOARD_H

/* this widget's custom signals */
typedef SigC::Signal3<void, int, int, float> type_signal_note_on;
typedef SigC::Signal2<void, int, int> type_signal_note_off;

class Keyboard : public Gtk::DrawingArea
{
public:
	Keyboard (void);
	~Keyboard (void);

	void SetChannel   (int argchan);
	void SetTranspose (int argtranspose);

	type_signal_note_on  signal_note_on  (void);
	type_signal_note_off signal_note_off (void);
protected:
	void drawKeyboard (int mode);
	void drawKeyboardFocus (void);

	/* overloaded signal handlers */
	virtual void on_realize              (void);
	virtual bool on_expose_event         (GdkEventExpose *e);
 	virtual bool on_focus_in_event       (GdkEventFocus  *f);
	virtual bool on_focus_out_event      (GdkEventFocus  *f);
	virtual bool on_button_press_event   (GdkEventButton *b);
	virtual bool on_button_release_event (GdkEventButton *b);
	virtual bool on_key_press_event      (GdkEventKey    *k);
	virtual bool on_key_release_event    (GdkEventKey    *k);

	thSynth *synth;
	int channel;
	int transpose;

	type_signal_note_on  m_signal_note_on;
	type_signal_note_off m_signal_note_off;
private:
	/* lower-level widget stuff */
	GdkWindow *drawable;
	GdkGC *kbgc;
	bool focus_box;

	/* keyboard stuff */
	int img_width, img_height;
	int prv_active_keys[128];
	int active_keys[128];
	
	bool ctrl_on;
	bool shift_on;
	bool alt_on;

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
