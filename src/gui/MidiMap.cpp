/* $Id: MidiMap.cpp,v 1.12 2004/11/09 09:37:40 ink Exp $ */
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

#include "config.h"

#include <gtkmm.h>
#include <stdio.h>

#include "think.h"
#include "MidiMap.h"

MidiMap::MidiMap (thSynth *argsynth)
{
	synth_ = argsynth;

	set_title("MIDI Controller Routing");

	mainVBox_ = manage(new Gtk::VBox);
	inputVBox_ = manage(new Gtk::VBox);
	newConnectionFrame_ = manage(new Gtk::Frame("Connection Source"));
	destinationFrame_ = manage(new Gtk::Frame("Connection Destination"));
	detailsFrame_ = manage(new Gtk::Frame("Connection Details"));
	connectFrame_ = manage(new Gtk::Frame("Connections"));
	srcDestHBox_ = manage(new Gtk::HBox);
	newConnectionHBox_ = manage(new Gtk::HBox);
	destinationHBox_ = manage(new Gtk::HBox);
	detailsHBox_ = manage(new Gtk::HBox);

	channelLbl_ = manage(new Gtk::Label("Midi Channel"));
	channelAdj_ = manage(new Gtk::Adjustment(1, 1, 16));
	channelSpinBtn_ = manage(new Gtk::SpinButton(*channelAdj_, 1, 0));
	channelSpinBtn_->signal_value_changed().connect(sigc::mem_fun(*this,&MidiMap::onChannelChanged));

	controllerLbl_ = manage(new Gtk::Label("Controller"));
	controllerAdj_ = manage(new Gtk::Adjustment(1, 1, 128));
	controllerSpinBtn_ = manage(new Gtk::SpinButton(*controllerAdj_, 1, 0));
	controllerSpinBtn_->signal_value_changed().connect(sigc::mem_fun(*this,&MidiMap::onControllerChanged));

	minLbl_ = manage(new Gtk::Label("Minimum"));
	minAdj_ = manage(new Gtk::Adjustment(0, 0, 0));
	minSpinBtn_ = manage(new Gtk::SpinButton(*minAdj_, .1, 4));
	minSpinBtn_->signal_value_changed().connect(sigc::mem_fun(
												*this,&MidiMap::onMinChanged));
	maxLbl_ = manage(new Gtk::Label("Maximum"));
	maxAdj_ = manage(new Gtk::Adjustment(0, 0, 0));
	maxSpinBtn_ = manage(new Gtk::SpinButton(*maxAdj_, .1, 4));
	maxSpinBtn_->signal_value_changed().connect(sigc::mem_fun(
												*this,&MidiMap::onMaxChanged));

	addBtn_ = manage(new Gtk::Button("Add Connection"));
	addBtn_->signal_clicked().connect(sigc::mem_fun(*this,
												   &MidiMap::onAddButton));

	destChanCombo_ = manage(new Gtk::Combo);
	fillDestChanCombo();

	destArgCombo_ = manage(new Gtk::Combo);
	fillDestArgCombo(0);

	add(*mainVBox_);

	newConnectionHBox_->pack_start(*channelLbl_, Gtk::PACK_EXPAND_WIDGET);
	newConnectionHBox_->pack_start(*channelSpinBtn_, Gtk::PACK_SHRINK);
	newConnectionHBox_->pack_start(*controllerLbl_, Gtk::PACK_EXPAND_WIDGET);
	newConnectionHBox_->pack_start(*controllerSpinBtn_, Gtk::PACK_SHRINK);
	newConnectionFrame_->add(*newConnectionHBox_);

	destinationHBox_->pack_start(*destChanCombo_, Gtk::PACK_EXPAND_WIDGET);
	destinationHBox_->pack_start(*destArgCombo_, Gtk::PACK_EXPAND_WIDGET);
	destinationFrame_->add(*destinationHBox_);

	srcDestHBox_->pack_start(*newConnectionFrame_, Gtk::PACK_EXPAND_WIDGET);
	srcDestHBox_->pack_start(*destinationFrame_, Gtk::PACK_EXPAND_WIDGET);

	detailsHBox_->pack_start(*minLbl_, Gtk::PACK_EXPAND_WIDGET);
	detailsHBox_->pack_start(*minSpinBtn_, Gtk::PACK_SHRINK);
	detailsHBox_->pack_start(*maxLbl_, Gtk::PACK_EXPAND_WIDGET);
	detailsHBox_->pack_start(*maxSpinBtn_, Gtk::PACK_SHRINK);
	detailsFrame_->add(*detailsHBox_);

	connectScroll_.add(connectView_);
	connectScroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);

	connectView_.signal_button_press_event().connect_notify(
		sigc::mem_fun(*this, &MidiMap::onConnectionSelected));
	connectView_.signal_cursor_changed().connect_notify(
		sigc::mem_fun(*this, &MidiMap::onConnectionMoved));
	connectFrame_->add(connectScroll_);

	connectModel_ = Gtk::ListStore::create (connectViewCols_);
	connectView_.set_model(connectModel_);

	populateConnections();
	connectView_.append_column("Channel", connectViewCols_.midiChan);
	connectView_.append_column("Controller", connectViewCols_.midiController);
	connectView_.append_column("Instrument", connectViewCols_.instrument);
	connectView_.append_column("Parameter", connectViewCols_.argName);

	inputVBox_->pack_start(*srcDestHBox_, Gtk::PACK_EXPAND_WIDGET);
	inputVBox_->pack_start(*detailsFrame_, Gtk::PACK_EXPAND_WIDGET);
	inputVBox_->pack_start(*addBtn_, Gtk::PACK_EXPAND_WIDGET);

	mainVBox_->pack_start(*inputVBox_, Gtk::PACK_SHRINK);
	mainVBox_->pack_start(*connectFrame_, Gtk::PACK_EXPAND_WIDGET);

	show_all_children();
}

MidiMap::~MidiMap (void)
{
}

void MidiMap::set_sensitive (bool sensitive)
{
	destArgCombo_->set_sensitive(sensitive);
	addBtn_->set_sensitive(sensitive);
	minSpinBtn_->set_sensitive(sensitive);
	maxSpinBtn_->set_sensitive(sensitive);
}

void MidiMap::fillDestChanCombo (void)
{
	Gtk::ComboDropDownItem *item;
	Gtk::Label *namelabel;
	std::map<int, string> *patchList = synth_->GetPatchlist();
	Gtk::ComboDropDown_Helpers::ComboDropDownList destChanComboStrings =
		destChanCombo_->get_list()->children();

	destChanComboStrings.clear();

	for (std::map<int, string>::iterator i = patchList->begin();
		 i != patchList->end(); i++)
	{
		item = Gtk::manage(new Gtk::ComboDropDownItem);
		namelabel = Gtk::manage(new Gtk::Label(g_strdup_printf("%d: %s", i->first + 1, basename(i->second.c_str()))));
		item->add(*namelabel);
		item->signal_button_press_event().connect(sigc::bind<int>(sigc::mem_fun(*this,&MidiMap::onDestChanComboChanged), i->first));
		item->signal_focus_in_event().connect(sigc::bind<int>(sigc::mem_fun(*this,&MidiMap::onDestChanComboFocus), i->first));
		item->show_all();
		destChanComboStrings.push_back(*item);		
	}
}

void MidiMap::fillDestArgCombo (int chan)
{
	int visibleArgs = 0;
	Gtk::ComboDropDownItem *item;
	Gtk::Label *namelabel;
	Gtk::ComboDropDown_Helpers::ComboDropDownList destArgComboStrings =
		destArgCombo_->get_list()->children();
	if(synth_->GetChannel(chan))
	{
		std::map<string, thArg *> argList = synth_->GetChanArgs(chan);
		destArgComboStrings.clear();
		
		for (std::map<string, thArg *>::iterator i = argList.begin();
			 i != argList.end(); i++)
		{
			if(i->second && i->second->getWidgetType() == thArg::SLIDER) {
				item = Gtk::manage(new Gtk::ComboDropDownItem);
				namelabel = Gtk::manage(new Gtk::Label(
								(i->second->getLabel().length() > 0) ?
								i->second->getLabel() : i->second->getName()));
				item->add(*namelabel);
				item->signal_button_press_event().connect(
					sigc::bind<thArg *>(sigc::mem_fun(*this,
								&MidiMap::onDestArgComboChanged), i->second));
				item->signal_focus_in_event().connect(
					sigc::bind<thArg *>(sigc::mem_fun(*this,
								&MidiMap::onDestArgComboFocus), i->second));
				item->show_all();
				destArgComboStrings.push_back(*item);
				if(!visibleArgs)
				{
					visibleArgs = 1;
					selectedArg_ = i->second;
				}
			}
		}

		if (visibleArgs)
		{
			set_sensitive(true);
			selectedMin_ = selectedArg_->getMin();
			selectedMax_ = selectedArg_->getMax();
			minSpinBtn_->set_range(selectedMin_, selectedMax_);
			maxSpinBtn_->set_range(selectedMin_, selectedMax_);
			minSpinBtn_->set_value(selectedMin_);
			maxSpinBtn_->set_value(selectedMax_);
		}
		else
		{
			set_sensitive(false);
		}
	}
	else
	{
		set_sensitive(false);
	}
}

void MidiMap::populateConnections (void)
{
}

void MidiMap::onChannelChanged (void)
{
	selectedChan_ = (int)channelSpinBtn_->get_value() - 1;
}

void MidiMap::onControllerChanged (void)
{
	selectedController_ = (int)controllerSpinBtn_->get_value() - 1;
}

bool MidiMap::onDestChanComboChanged (GdkEventButton* b, int chan)
{
	fillDestArgCombo(chan);
	return true;
}

bool MidiMap::onDestArgComboChanged (GdkEventButton* b, thArg *arg)
{
	selectedArg_ = arg;
	selectedMin_ = arg->getMin();
	selectedMax_ = arg->getMax();
	minSpinBtn_->set_range(selectedMin_, selectedMax_);
	maxSpinBtn_->set_range(selectedMin_, selectedMax_);
	minSpinBtn_->set_value(selectedMin_);
	maxSpinBtn_->set_value(selectedMax_);
	return true;
}

void MidiMap::onMinChanged (void)
{
	selectedMin_ = minSpinBtn_->get_value();
}

void MidiMap::onMaxChanged (void)
{
	selectedMax_ = maxSpinBtn_->get_value();
}

void MidiMap::onConnectionSelected (GdkEventButton *b)
{
}

void MidiMap::onConnectionMoved (void)
{
}

void MidiMap::onAddButton (void)
{
	synth_->newMidiControllerConnection((unsigned char)selectedChan_, (unsigned int)selectedController_, new thMidiControllerConnection(selectedArg_, selectedMin_, selectedMax_));
}
