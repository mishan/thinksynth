#ifndef KEYBOARD_WINDOW_H
#define KEYBOARD_WINDOW_H

class KeyboardWindow : public Gtk::Window
{
public:
	KeyboardWindow (thSynth *argsynth);
	~KeyboardWindow (void);
};

/* XXX */
extern void create_keyboard_window (void);
extern void draw_keyboard (int mode);

#endif /* KEYBOARD_WINDOW_H */
