/* $Id: PatchSelWindow.h,v 1.6 2004/03/27 07:28:47 misha Exp $ */

#ifndef PATCHSEL_WINDOW_H
#define PATCHSEL_WINDOW_H

class PatchSelColumns : public Gtk::TreeModel::ColumnRecord
{
public:
	PatchSelColumns (void)
	{
		add (chanNum);
		add (dspName);
		add (amp);
	}

	Gtk::TreeModelColumn <unsigned int> chanNum;
	Gtk::TreeModelColumn <Glib::ustring> dspName;
	Gtk::TreeModelColumn <float> amp;
};

class PatchSelWindow : public Gtk::Window
{
public:
	PatchSelWindow (thSynth *synth);
	~PatchSelWindow (void);

protected:
	void LoadPatch (void);
	void SetChannelAmp (void);

	void patchSelected (GdkEventButton *b);

	Gtk::VBox vbox;
	Gtk::HBox controlHbox;

	Gtk::HScale dspAmp;
	Gtk::Entry fileEntry;
	Gtk::Button setButton;

	Gtk::ScrolledWindow patchScroll;
	Gtk::TreeView patchView;
	Glib::RefPtr<Gtk::ListStore> patchModel;
	PatchSelColumns patchViewCols;
private:
	thSynth *realSynth, *mySynth;
};

#endif /* PATCHSEL_WINDOW_H */
