/* $Id: MainSynthWindow.h,v 1.7 2004/08/07 18:54:49 misha Exp $ */

#ifndef MAIN_SYNTH_WINDOW_H
#define MAIN_SYNTH_WINDOW_H

using namespace std;

class MainSynthWindow : public Gtk::Window
{
public:
	MainSynthWindow (thSynth *);
	~MainSynthWindow (void);

protected:
	void menuKeyboard (void);
	void menuPatchSel (void);
	void menuQuit (void);
	void menuAbout (void);

	void sliderChanged (Gtk::HScale *, thArg *);

	void populate (void);

	Gtk::VBox vbox;
	Gtk::MenuBar menuBar;
	Gtk::Menu menuFile;
	Gtk::Menu menuHelp;
	Gtk::Notebook notebook;

	PatchSelWindow *patchSel;
//	KeyboardWindow keyboardWin;
private:
	thSynth *synth;
};

#endif /* MAIN_SYNTH_WINDOW_H */
