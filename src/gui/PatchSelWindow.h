/* $Id: PatchSelWindow.h,v 1.5 2004/03/27 03:28:52 misha Exp $ */

#ifndef PATCHSEL_WINDOW_H
#define PATCHSEL_WINDOW_H

class PatchSelWindow : public Gtk::Window
{
public:
	PatchSelWindow (thSynth *synth);
	~PatchSelWindow (void);

protected:
	void LoadPatch (Gtk::Entry *chanEntry, thSynth *synth);
	void SetChannelAmp (Gtk::HScale *scale, thSynth *synth);

	Gtk::VBox vbox;

	Gtk::Table patchTable;	
private:
	thSynth *realSynth, *mySynth;
};

#endif /* PATCHSEL_WINDOW_H */
