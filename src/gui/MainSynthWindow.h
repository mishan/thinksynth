/* $Id: MainSynthWindow.h,v 1.4 2004/04/07 07:23:16 misha Exp $ */

#ifndef MAIN_SYNTH_WINDOW_H
#define MAIN_SYNTH_WINDOW_H

class MainSynthWindow : public Gtk::Window
{
public:
	MainSynthWindow (thSynth *synth);
	~MainSynthWindow (void);

protected:
	void menuKeyboard (void);
	void menuPatchSel (void);
	void menuQuit (void);
	void menuAbout (void);

	Gtk::VBox vbox;
	Gtk::MenuBar menuBar;
	Gtk::Menu menuFile;
	Gtk::Menu menuHelp;

	PatchSelWindow patchSel;
//	KeyboardWindow keyboardWin;
private:
	thSynth *realSynth;
};

#endif /* MAIN_SYNTH_WINDOW_H */
