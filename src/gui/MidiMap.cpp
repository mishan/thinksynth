/* $Id: MidiMap.cpp,v 1.9 2004/11/09 08:07:41 ink Exp $ */
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
	synth = argsynth;

	set_title("MIDI Controller Routing");

	mainVBox = manage(new Gtk::VBox);
	newConnectionFrame = manage(new Gtk::Frame("Connection Source"));
	destinationFrame = manage(new Gtk::Frame("Connection Destination"));
	detailsFrame = manage(new Gtk::Frame("Connection Details"));
	newConnectionHBox = manage(new Gtk::HBox);
	destinationHBox = manage(new Gtk::HBox);
	detailsHBox = manage(new Gtk::HBox);

	channelLbl = manage(new Gtk::Label("Midi Channel"));
	channelAdj = manage(new Gtk::Adjustment(1, 1, 16));
	channelSpinBtn = manage(new Gtk::SpinButton(*channelAdj, 1, 0));
	channelSpinBtn->signal_value_changed().connect(sigc::mem_fun(*this,&MidiMap::onChannelChanged));

	controllerLbl = manage(new Gtk::Label("Controller"));
	controllerAdj = manage(new Gtk::Adjustment(1, 1, 128));
	controllerSpinBtn = manage(new Gtk::SpinButton(*controllerAdj, 1, 0));
	controllerSpinBtn->signal_value_changed().connect(sigc::mem_fun(*this,&MidiMap::onControllerChanged));

	minLbl = manage(new Gtk::Label("Minimum"));
	minAdj = manage(new Gtk::Adjustment(0, 0, 0));
	minSpinBtn = manage(new Gtk::SpinButton(*minAdj, .1, 4));
	minSpinBtn->signal_value_changed().connect(sigc::mem_fun(
												*this,&MidiMap::onMinChanged));
	maxLbl = manage(new Gtk::Label("Maximum"));
	maxAdj = manage(new Gtk::Adjustment(0, 0, 0));
	maxSpinBtn = manage(new Gtk::SpinButton(*maxAdj, .1, 4));
	maxSpinBtn->signal_value_changed().connect(sigc::mem_fun(
												*this,&MidiMap::onMaxChanged));

	addBtn = manage(new Gtk::Button("Add Connection"));
	addBtn->signal_clicked().connect(sigc::mem_fun(*this,
												   &MidiMap::onAddButton));

	destChanCombo = manage(new Gtk::Combo);
	fillDestChanCombo();

	destArgCombo = manage(new Gtk::Combo);
	fillDestArgCombo(0);

	add(*mainVBox);

	newConnectionHBox->pack_start(*channelLbl, Gtk::PACK_EXPAND_WIDGET);
	newConnectionHBox->pack_start(*channelSpinBtn, Gtk::PACK_EXPAND_WIDGET);
	newConnectionHBox->pack_start(*controllerLbl, Gtk::PACK_EXPAND_WIDGET);
	newConnectionHBox->pack_start(*controllerSpinBtn, Gtk::PACK_EXPAND_WIDGET);
	newConnectionFrame->add(*newConnectionHBox);

	destinationHBox->pack_start(*destChanCombo, Gtk::PACK_EXPAND_WIDGET);
	destinationHBox->pack_start(*destArgCombo, Gtk::PACK_EXPAND_WIDGET);
	destinationFrame->add(*destinationHBox);

	detailsHBox->pack_start(*minLbl, Gtk::PACK_EXPAND_WIDGET);
	detailsHBox->pack_start(*minSpinBtn, Gtk::PACK_EXPAND_WIDGET);
	detailsHBox->pack_start(*maxLbl, Gtk::PACK_EXPAND_WIDGET);
	detailsHBox->pack_start(*maxSpinBtn, Gtk::PACK_EXPAND_WIDGET);
	detailsFrame->add(*detailsHBox);

	mainVBox->pack_start(*newConnectionFrame, Gtk::PACK_EXPAND_WIDGET);
	mainVBox->pack_start(*destinationFrame, Gtk::PACK_EXPAND_WIDGET);
	mainVBox->pack_start(*detailsFrame, Gtk::PACK_EXPAND_WIDGET);
	mainVBox->pack_start(*addBtn, Gtk::PACK_EXPAND_WIDGET);


	show_all_children();
}

MidiMap::~MidiMap (void)
{
}

void MidiMap::set_sensitive (bool sensitive)
{
	destArgCombo->set_sensitive(sensitive);
	addBtn->set_sensitive(sensitive);
	minSpinBtn->set_sensitive(sensitive);
	maxSpinBtn->set_sensitive(sensitive);
}

void MidiMap::fillDestChanCombo (void)
{
	Gtk::ComboDropDownItem *item;
	Gtk::Label *namelabel;
	std::map<int, string> *patchList = synth->GetPatchlist();
	Gtk::ComboDropDown_Helpers::ComboDropDownList destChanComboStrings =
		destChanCombo->get_list()->children();

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
		destArgCombo->get_list()->children();
	if(synth->GetChannel(chan))
	{
		std::map<string, thArg *> argList = synth->GetChanArgs(chan);
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
					selectedArg = i->second;
				}
			}
		}

		if (visibleArgs)
		{
			set_sensitive(true);
			selectedMin = selectedArg->getMin();
			selectedMax = selectedArg->getMax();
			minSpinBtn->set_range(selectedMin, selectedMax);
			maxSpinBtn->set_range(selectedMin, selectedMax);
			minSpinBtn->set_value(selectedMin);
			maxSpinBtn->set_value(selectedMax);
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

void MidiMap::onChannelChanged (void)
{
	selectedChan = (int)channelSpinBtn->get_value() - 1;
}

void MidiMap::onControllerChanged (void)
{
	selectedController = (int)controllerSpinBtn->get_value() - 1;
}

bool MidiMap::onDestChanComboChanged (GdkEventButton* b, int chan)
{
	fillDestArgCombo(chan);
	return true;
}

bool MidiMap::onDestArgComboChanged (GdkEventButton* b, thArg *arg)
{
	selectedArg = arg;
	selectedMin = arg->getMin();
	selectedMax = arg->getMax();
	minSpinBtn->set_range(selectedMin, selectedMax);
	maxSpinBtn->set_range(selectedMin, selectedMax);
	minSpinBtn->set_value(selectedMin);
	maxSpinBtn->set_value(selectedMax);
	return true;
}

void MidiMap::onMinChanged (void)
{
	selectedMin = minSpinBtn->get_value();
}

void MidiMap::onMaxChanged (void)
{
	selectedMax = maxSpinBtn->get_value();
}

void MidiMap::onAddButton (void)
{
	synth->newMidiControllerConnection((unsigned char)selectedChan, (unsigned int)selectedController, new thMidiControllerConnection(selectedArg, selectedMin, selectedMax));
}
