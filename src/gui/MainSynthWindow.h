/* $Id: MainSynthWindow.h,v 1.9 2004/09/05 00:21:48 misha Exp $ */
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

using namespace std;

class MainSynthWindow : public Gtk::Window
{
public:
	MainSynthWindow (thSynth *);
	~MainSynthWindow (void);

protected:
	void populateMenu (void);
	void menuKeyboard (void);
	void menuPatchSel (void);
	void menuQuit (void);
	void menuAbout (void);

	void sliderChanged (Gtk::HScale *, thArg *);

	void populate (void);

	void channelChanged (string filename, int chan, float amp);

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
