#ifndef PATCHSEL_WINDOW_H
#define PATCHSEL_WINDOW_H

class PatchSelWindow : public Gtk::Window
{
public:
	PatchSelWindow (thSynth *synth);

protected:
	void LoadPatch (Gtk::Entry *chanEntry, thSynth *synth);

	Gtk::VBox vbox;

	Gtk::Table patchTable;	
private:
	thSynth *realSynth, *mySynth;
};

#endif /* PATCHSEL_WINDOW_H */
