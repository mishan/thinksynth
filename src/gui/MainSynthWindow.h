/* $Id: MainSynthWindow.h,v 1.2 2004/03/27 07:34:52 misha Exp $ */

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
	void menuAbout (void);

	Gtk::VBox vbox;
	Gtk::MenuBar menuBar;
	Gtk::Menu menuFile;
	Gtk::Menu menuHelp;

	PatchSelWindow patchSel;
private:
	thSynth *realSynth;
};

#endif /* MAIN_SYNTH_WINDOW_H */
