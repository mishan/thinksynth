/* $Id: MidiMap.h,v 1.6 2004/11/09 07:20:43 ink Exp $ */
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

#ifndef MIDI_MAP_H
#define MIDI_MAP_H

class MidiMap : public Gtk::Window
{
public:
	MidiMap (thSynth *);
	~MidiMap (void);

	void set_sensitive (bool sensitive);

protected:
	void onAddButton (void);
	void onChannelChanged (void);
	void onControllerChanged (void);
	bool onDestChanComboChanged (GdkEventButton* b, int chan);
	bool onDestArgComboChanged (GdkEventButton* b, thArg *arg);
	void onMinChanged (void);
	void onMaxChanged (void);

	Gtk::Adjustment *channelAdj;
	Gtk::Adjustment *controllerAdj;
	Gtk::Adjustment *minAdj;
	Gtk::Adjustment *maxAdj;

	Gtk::VBox *mainVBox;
	Gtk::HBox *newConnectionHBox;
	Gtk::Frame *newConnectionFrame;
	Gtk::HBox *destinationHBox;
	Gtk::Frame *destinationFrame;
	Gtk::HBox *detailsHBox;
	Gtk::Frame *detailsFrame;
	Gtk::Label *channelLbl;
	Gtk::SpinButton *channelSpinBtn;
	Gtk::Label *controllerLbl;
	Gtk::SpinButton *controllerSpinBtn;
	Gtk::Combo *destChanCombo;
	Gtk::Combo *destArgCombo;
	Gtk::Label *minLbl;
	Gtk::SpinButton *minSpinBtn;
	Gtk::Label *maxLbl;
	Gtk::SpinButton *maxSpinBtn;
	Gtk::Button *addBtn;

private:
	void fillDestChanCombo (void);
	void fillDestArgCombo (int chan);

	thSynth *synth;
	int selectedChan;
	int selectedController;
	float selectedMin;
	float selectedMax;
	thArg *selectedArg;
};

#endif /* ABOUT_BOX_H */
