/* $Id$ */
/*
 * Copyright (C) 2004 Metaphonic Labs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef MAIN_SYNTH_WINDOW_H
#define MAIN_SYNTH_WINDOW_H

class gthAudio;
class gthPrefs;
class AboutBox;
class MidiMap;

using namespace std;

class MainSynthWindow : public Gtk::Window
{
public:
	MainSynthWindow (gthAudio *);
	~MainSynthWindow (void);

protected:
	void populateMenu (void);
	void menuKeyboard (void);
	void menuPatchSel (void);
	void menuMidiMap (void);
	void menuQuit (void);
	void menuAbout (void);
	void menuJackTry (void);
	void menuJackDis (void);
	void menuJackAuto (void);

	void append_tab (const string &tabName, int num, bool is_real);
	void populate (void);

	void onPatchesChanged (void);
	void onAboutBoxHide (void);
	void onPatchSelHide (void);
	void onMidiMapHide (void);
	void onKeyboardHide (KeyboardWindow *kbwin);
	void onSwitchPage (GtkNotebookPage *p, int pagenum);
	void onDspEntryActivate (void);
	void onBrowseButton (void);
	void onPatchLoadError (const char* failure);
	void jackCheck (void);

	Gtk::VBox vbox_;
	Gtk::MenuBar menuBar_;
	Gtk::Menu menuFile_;
	Gtk::Menu menuJack_;
	Gtk::Menu menuHelp_;

	Gtk::Entry dspEntry_;
	Gtk::Label dspEntryLbl_;
	Gtk::Button dspBrowseBtn_;
	Gtk::HBox dspEntryBox_;

	Gtk::Notebook notebook_;

	PatchSelWindow *patchSel_;
	AboutBox *aboutBox_;
	MidiMap *midiMap_;
private:
	gthAudio *audio_;
	char *prevDir_;

	void toggleConnects(void);
};

#endif /* MAIN_SYNTH_WINDOW_H */
