#ifndef KEYBOARD_WINDOW_H
#define KEYBOARD_WINDOW_H

class KeyboardWindow : public Gtk::Window
{
public:
	KeyboardWindow (thSynth *argsynth);
	~KeyboardWindow (void);

protected:
	bool clickEvent (GdkEventButton *b);
	bool exposeEvent (GdkEventExpose *e);
	void drawKeyboard (int mode);

	Gtk::DrawingArea drawArea;

	GdkWindow *drawable;
	GdkGC		*kbgc;

	int img_width, img_height;
	int prv_active_keys[128];
	int active_keys[128];
	
	int mouse_notnum;
	int transpose;
	int key_ofs;
	int veloc0;
	int veloc1;
	int veloc2;
	int veloc3;
	int mouse_veloc;

	int cur_size;

	thSynth *synth;
private:
	int get_coord ();
};

/* XXX */
extern void create_keyboard_window (void);
extern void draw_keyboard (int mode);

#endif /* KEYBOARD_WINDOW_H */
