/* $Id: Keyboard.h,v 1.8 2004/04/06 09:25:32 misha Exp $ */

#ifndef KEYBOARD_H
#define KEYBOARD_H

/* this widget's custom signals */
typedef SigC::Signal3<void, int, int, float> type_signal_note_on;
typedef SigC::Signal2<void, int, int>        type_signal_note_off;
typedef SigC::Signal1<void, int>             type_signal_channel_changed;
typedef SigC::Signal1<void, int>             type_signal_transpose_changed;

class Keyboard : public Gtk::DrawingArea
{
public:
	Keyboard (void);
	~Keyboard (void);

	void SetChannel   (int argchan);
	void SetTranspose (int argtranspose);
	void SetNote (int note, bool state);

	type_signal_note_on           signal_note_on  (void);
	type_signal_note_off          signal_note_off (void);
	type_signal_channel_changed   signal_channel_changed (void);
	type_signal_transpose_changed signal_transpose_changed (void);

protected:
	void drawKeyboard (int mode);
	void drawKeyboardFocus (void);

	/* overridden signal handlers */
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
private:
	type_signal_note_on           m_signal_note_on;
	type_signal_note_off          m_signal_note_off;
	type_signal_channel_changed   m_signal_channel_changed;
	type_signal_transpose_changed m_signal_transpose_changed;

	/* lower-level widget stuff */
	Glib::Mutex drawMutex;

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
	void adjust_transpose (int n);
	void adjust_velocity (int m, int n);
	void fkeys_func (int key);
};

#endif /* KEYBOARD_H */
