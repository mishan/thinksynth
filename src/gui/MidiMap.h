/* $Id: MidiMap.h,v 1.14 2004/11/11 01:07:44 ink Exp $ */
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

class MidiMapColumns : public Gtk::TreeModel::ColumnRecord
{
public:
	MidiMapColumns (void)
		{
			add (midiChan);
			add (midiController);
			add (instrument);
			add (argName);
		}
	Gtk::TreeModelColumn <int> midiChan;
	Gtk::TreeModelColumn <int> midiController;
	Gtk::TreeModelColumn <string> instrument;
	Gtk::TreeModelColumn <string> argName;
};


class MidiMap : public Gtk::Window
{
public:
	MidiMap (thSynth *);
	~MidiMap (void);

	void set_sensitive (bool sensitive);

protected:
	void onAddButton (void);
	void onDelButton (void);
	void onChannelChanged (void);
	void onControllerChanged (void);
	bool onDestChanComboChanged (GdkEventButton* b, int chan, string instrument);
	bool onDestArgComboChanged (GdkEventButton* b, thArg *arg);
	bool onDestChanComboFocus (GdkEventFocus* f, int chan, string instrument)
		{ return onDestChanComboChanged(NULL, chan, instrument); }
	bool onDestArgComboFocus (GdkEventFocus* f, thArg *arg)
		{ return onDestArgComboChanged(NULL, arg); }
	void onMinChanged (void);
	void onMaxChanged (void);
	void onExpToggled (void);
	void onConnectionSelected (GdkEventButton *b);
	void onConnectionMoved (void);

	Gtk::Adjustment *channelAdj_;
	Gtk::Adjustment *controllerAdj_;
	Gtk::Adjustment *minAdj_;
	Gtk::Adjustment *maxAdj_;

	Gtk::VBox *mainVBox_;
	Gtk::VBox *inputVBox_;
	Gtk::HBox *srcDestHBox_;
	Gtk::HBox *newConnectionHBox_;
	Gtk::Frame *newConnectionFrame_;
	Gtk::HBox *destinationHBox_;
	Gtk::Frame *destinationFrame_;
	Gtk::HBox *detailsHBox_;
	Gtk::Frame *detailsFrame_;
	Gtk::HBox *buttonsHBox_;
	Gtk::Label *channelLbl_;
	Gtk::SpinButton *channelSpinBtn_;
	Gtk::Label *controllerLbl_;
	Gtk::SpinButton *controllerSpinBtn_;
	Gtk::Combo *destChanCombo_;
	Gtk::Combo *destArgCombo_;
	Gtk::Label *minLbl_;
	Gtk::SpinButton *minSpinBtn_;
	Gtk::Label *maxLbl_;
	Gtk::SpinButton *maxSpinBtn_;
	Gtk::Label *expLbl_;
	Gtk::CheckButton *expCheckBtn_;
	Gtk::Button *addBtn_;
	Gtk::Button *delBtn_;

	Gtk::Frame *connectFrame_;
	Gtk::ScrolledWindow connectScroll_;
	Gtk::TreeView connectView_;
	Glib::RefPtr<Gtk::ListStore> connectModel_;
	MidiMapColumns connectViewCols_;

private:
	void fillDestChanCombo (void);
	void setDestChanCombo (void);
	void fillDestArgCombo (int chan);
	void setDestArgCombo (int chan);
	void populateConnections (void);

	thSynth *synth_;
	int selectedChan_;
	int selectedController_;
	int selectedDestChan_;
	string selectedInstrument_;
	float selectedMin_;
	float selectedMax_;
	int selectedExp_;
	thArg *selectedArg_;
};

#endif /* ABOUT_BOX_H */
