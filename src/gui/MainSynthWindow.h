/* $Id: MainSynthWindow.h,v 1.1 2004/03/27 03:28:52 misha Exp $ */

#ifndef MAIN_SYNTH_WINDOW_H
#define MAIN_SYNTH_WINDOW_H

class MainSynthWindow : public Gtk::Window
{
public:
	MainSynthWindow (thSynth *synth);
	~MainSynthWindow (void);

protected:
	void menuPatchSel (void);
	void menuQuit (void);

	Gtk::VBox vbox;
	Gtk::MenuBar menuBar;
	Gtk::Menu menuFile;
	Gtk::Menu menuHelp;

	PatchSelWindow patchSel;
private:
	thSynth *realSynth;
};

#endif /* MAIN_SYNTH_WINDOW_H */
