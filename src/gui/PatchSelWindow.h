/* $Id: PatchSelWindow.h,v 1.7 2004/03/27 09:33:38 misha Exp $ */

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
	void BrowsePatch (void);

	void patchSelected (GdkEventButton *b);

	Gtk::VBox vbox;
	Gtk::Table controlTable;

	Gtk::HScale dspAmp;
	Gtk::Button setButton;
	Gtk::Button browseButton;
	Gtk::Label ampLabel;

	Gtk::Label fileLabel;
	Gtk::Entry fileEntry;

	Gtk::ScrolledWindow patchScroll;
	Gtk::TreeView patchView;
	Glib::RefPtr<Gtk::ListStore> patchModel;
	PatchSelColumns patchViewCols;
private:
	thSynth *realSynth, *mySynth;
};

#endif /* PATCHSEL_WINDOW_H */
