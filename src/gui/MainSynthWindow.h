/* $Id: MainSynthWindow.h,v 1.6 2004/08/01 11:05:18 misha Exp $ */

#ifndef MAIN_SYNTH_WINDOW_H
#define MAIN_SYNTH_WINDOW_H

class MainSynthWindow : public Gtk::Window
{
public:
	MainSynthWindow (thSynth *synth);
	~MainSynthWindow (void);

protected:
	void sliderChanged (Gtk::HScale *, thArg *);
	void menuKeyboard (void);
	void menuPatchSel (void);
	void menuQuit (void);
	void menuAbout (void);

	Gtk::VBox vbox;
	Gtk::MenuBar menuBar;
	Gtk::Menu menuFile;
	Gtk::Menu menuHelp;
	Gtk::Notebook notebook;

	PatchSelWindow patchSel;
//	KeyboardWindow keyboardWin;
private:
	thSynth *realSynth;
};

#endif /* MAIN_SYNTH_WINDOW_H */
