/*
 * Copyright (C) 2004-2026 Metaphonic Labs
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

#include <iostream>
#include <sstream>

#include <gtkmm.h>
#include <stdio.h>

#include "think.h"
#include "gthPatchfile.h"
#include "MidiMap.h"
#include "ColumnUtil.h"

MidiMap::MidiMap (thSynth *argsynth)
{
    rebuilding_ = false;

    /* All of these are read before anything necessarily sets them -- with no
       patch loaded, fillDestArgCombo() finds no args and leaves selectedArg_
       alone, and onAddButton() would then act on whatever was in the memory.
       -1 for the channel so it cannot accidentally name channel 0. */
    selectedDestChan_ = -1;
    selectedArg_ = NULL;
    selectedMin_ = 0;
    selectedMax_ = 0;
    selectedExp_ = 0;

    gthPatchManager *patchMgr = gthPatchManager::instance();

    synth_ = argsynth;

    set_title("thinksynth - MIDI Controller Routing");

    mainVBox_ = manage(new Gtk::Box(Gtk::Orientation::VERTICAL));
    inputVBox_ = manage(new Gtk::Box(Gtk::Orientation::VERTICAL, 0));
    inputVBox_->set_size_request(700, 140);
    newConnectionFrame_ = manage(new Gtk::Frame("Connection Source"));
    destinationFrame_ = manage(new Gtk::Frame("Connection Destination"));
    detailsFrame_ = manage(new Gtk::Frame("Connection Details"));
    connectFrame_ = manage(new Gtk::Frame("Connections"));
    srcDestHBox_ = manage(new Gtk::Box(Gtk::Orientation::HORIZONTAL, 3));
    srcDestHBox_->set_homogeneous(true);
    newConnectionHBox_ = manage(new Gtk::Box(Gtk::Orientation::HORIZONTAL, 0));
    destinationHBox_ = manage(new Gtk::Box(Gtk::Orientation::HORIZONTAL, 4));
    destinationHBox_->set_homogeneous(true);
    detailsHBox_ = manage(new Gtk::Box(Gtk::Orientation::HORIZONTAL, 0));
    detailsHBox_->set_homogeneous(true);
    buttonsHBox_ = manage(new Gtk::Box(Gtk::Orientation::HORIZONTAL, 0));
    buttonsHBox_->set_homogeneous(true);

    channelLbl_ = manage(new Gtk::Label("Midi Channel"));
    channelAdj_ = Gtk::Adjustment::create(1, 1, 16);
    channelSpinBtn_ = manage(new Gtk::SpinButton(channelAdj_, 1, 0));
    channelSpinBtn_->signal_value_changed().connect(
        sigc::mem_fun(*this,&MidiMap::onChannelChanged));
    selectedChan_ = 0;

    controllerLbl_ = manage(new Gtk::Label("Controller"));
    controllerAdj_ = Gtk::Adjustment::create(0, 0, 127);
    controllerSpinBtn_ = manage(new Gtk::SpinButton(controllerAdj_, 1, 0));
    controllerSpinBtn_->signal_value_changed().connect(
        sigc::mem_fun(*this,&MidiMap::onControllerChanged));
    selectedController_ = 0;

    minLbl_ = manage(new Gtk::Label("Minimum"));
    minAdj_ = Gtk::Adjustment::create(0, 0, 0);
    minSpinBtn_ = manage(new Gtk::SpinButton(minAdj_, .1, 4));
    minSpinBtn_->signal_value_changed().connect(sigc::mem_fun(
                                                *this,&MidiMap::onMinChanged));
    maxLbl_ = manage(new Gtk::Label("Maximum"));
    maxAdj_ = Gtk::Adjustment::create(0, 0, 0);
    maxSpinBtn_ = manage(new Gtk::SpinButton(maxAdj_, .1, 4));
    maxSpinBtn_->signal_value_changed().connect(sigc::mem_fun(
                                                *this,&MidiMap::onMaxChanged));

    expLbl_ = manage(new Gtk::Label("Exponential"));
    expCheckBtn_ = manage(new Gtk::CheckButton);
    expCheckBtn_->signal_toggled().connect(sigc::mem_fun(
                                                *this,&MidiMap::onExpToggled));

    addBtn_ = manage(new Gtk::Button("Add/Modify  Connection"));
    addBtn_->signal_clicked().connect(sigc::mem_fun(*this,
                                                   &MidiMap::onAddButton));
    delBtn_ = manage(new Gtk::Button("Remove Connection"));
    delBtn_->signal_clicked().connect(sigc::mem_fun(*this,
                                                   &MidiMap::onDelButton));

    /* Nothing is selected yet, and Remove is the one button that only means
       something against a selected connection. onConnectionMoved turns it on
       and off from there. */
    delBtn_->set_sensitive(false);
    addBtn_->set_hexpand(true);
    buttonsHBox_->append(*addBtn_);
    delBtn_->set_hexpand(true);
    buttonsHBox_->append(*delBtn_);

    destChanCombo_ = manage(new Gtk::ComboBoxText);
    destChanCombo_->signal_changed().connect(
        sigc::mem_fun(*this, &MidiMap::onDestChanSelected));
    fillDestChanCombo();

    destArgCombo_ = manage(new Gtk::ComboBoxText);
    destArgCombo_->signal_changed().connect(
        sigc::mem_fun(*this, &MidiMap::onDestArgSelected));
    fillDestArgCombo(selectedDestChan_);

    set_child(*mainVBox_);

    channelLbl_->set_hexpand(true);
    newConnectionHBox_->append(*channelLbl_);
    newConnectionHBox_->append(*channelSpinBtn_);
    controllerLbl_->set_hexpand(true);
    newConnectionHBox_->append(*controllerLbl_);
    newConnectionHBox_->append(*controllerSpinBtn_);
    newConnectionFrame_->set_child(*newConnectionHBox_);

    destChanCombo_->set_hexpand(true);
    destinationHBox_->append(*destChanCombo_);
    destArgCombo_->set_hexpand(true);
    destinationHBox_->append(*destArgCombo_);
    destinationFrame_->set_child(*destinationHBox_);

    newConnectionFrame_->set_hexpand(true);
    srcDestHBox_->append(*newConnectionFrame_);
    destinationFrame_->set_hexpand(true);
    srcDestHBox_->append(*destinationFrame_);

    minLbl_->set_hexpand(true);
    detailsHBox_->append(*minLbl_);
    detailsHBox_->append(*minSpinBtn_);
    maxLbl_->set_hexpand(true);
    detailsHBox_->append(*maxLbl_);
    detailsHBox_->append(*maxSpinBtn_);
    expLbl_->set_hexpand(true);
    detailsHBox_->append(*expLbl_);
    detailsHBox_->append(*expCheckBtn_);
    detailsFrame_->set_child(*detailsHBox_);

    connectScroll_.set_child(connectView_);
    connectScroll_.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    connectScroll_.set_size_request(700, 128);

    connectFrame_->set_child(connectScroll_);

    connectModel_ = Gio::ListStore<MidiMapRow>::create();
    connectSelection_ = Gtk::SingleSelection::create(connectModel_);

    /* Nothing selected until the user picks something. SingleSelection
       otherwise selects row 0 as soon as the model is filled, which would fire
       the handlers below and load a connection into the editor that the user
       never asked for. */
    connectSelection_->set_autoselect(false);
    connectSelection_->set_can_unselect(true);
    connectSelection_->set_selected(GTK_INVALID_LIST_POSITION);

    connectView_.set_model(connectSelection_);

    /* The row that is current, however it became current -- mouse or
       keyboard. The TreeView spelling was signal_cursor_changed; the
       ColumnView equivalent is the selection model saying so. */
    connectSelection_->property_selected().signal_changed().connect(
        sigc::mem_fun(*this, &MidiMap::onConnectionSelected));
    connectSelection_->property_selected().signal_changed().connect(
        sigc::mem_fun(*this, &MidiMap::onConnectionMoved));

    populateConnections();

    /* +1 on the channel, here rather than in the model: MIDI numbers channels
       from zero and everything a user reads numbers them from one. The
       controller is *not* shifted -- controller 0 is called 0 everywhere,
       including in every MIDI reference -- which is what the previous list
       showed and what this keeps. */
    connectView_.append_column(gthTextColumn("Channel",
        [](const Glib::RefPtr<Glib::ObjectBase> &o) {
            Glib::RefPtr<MidiMapRow> r =
                std::dynamic_pointer_cast<MidiMapRow>(o);
            return r ? Glib::ustring::format(r->chan() + 1) : Glib::ustring();
        }, Gtk::Align::END));

    connectView_.append_column(gthTextColumn("Controller",
        [](const Glib::RefPtr<Glib::ObjectBase> &o) {
            Glib::RefPtr<MidiMapRow> r =
                std::dynamic_pointer_cast<MidiMapRow>(o);
            return r ? Glib::ustring::format(r->controller()) : Glib::ustring();
        }, Gtk::Align::END));

    connectView_.append_column(gthTextColumn("Instrument",
        [](const Glib::RefPtr<Glib::ObjectBase> &o) {
            Glib::RefPtr<MidiMapRow> r =
                std::dynamic_pointer_cast<MidiMapRow>(o);
            return r ? r->instrument() : Glib::ustring();
        }));

    connectView_.append_column(gthTextColumn("Parameter",
        [](const Glib::RefPtr<Glib::ObjectBase> &o) {
            Glib::RefPtr<MidiMapRow> r =
                std::dynamic_pointer_cast<MidiMapRow>(o);
            return r ? r->argName() : Glib::ustring();
        }));

    srcDestHBox_->set_vexpand(true);
    inputVBox_->append(*srcDestHBox_);
    detailsFrame_->set_vexpand(true);
    inputVBox_->append(*detailsFrame_);
    buttonsHBox_->set_vexpand(true);
    inputVBox_->append(*buttonsHBox_);

    connectFrame_->set_vexpand(true);
    mainVBox_->append(*connectFrame_);
    mainVBox_->append(*inputVBox_);


    patchMgr->signal_patches_changed().connect(
        sigc::mem_fun(*this, &MidiMap::onPatchChanged));
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
    expCheckBtn_->set_sensitive(sensitive);
}

/* The old Gtk::Combo held a list of arbitrary widgets, so each entry could
 * carry its own button-press and focus handlers bound to the channel it stood
 * for. Gtk::ComboBoxText is model-backed: entries are (id, text) pairs and the
 * widget has a single signal_changed. The channel number therefore travels as
 * the entry's id and is recovered on selection, rather than being baked into
 * per-item signal bindings.
 */
void MidiMap::fillDestChanCombo (void)
{
    gthPatchManager *patchMgr = gthPatchManager::instance();
    int numPatches = patchMgr->numPatches();
    bool first = true;

    /* Rebuilding the model fires signal_changed; suppress it or selecting a
       channel would recurse back through here. */
    rebuilding_ = true;

    destChanCombo_->remove_all();

    for (int i = 0; i < numPatches; i++)
    {
        if (patchMgr->isLoaded(i) == false)
            continue;

        gthPatchManager::PatchFile *patch = patchMgr->getPatch(i);
        std::ostringstream chanStr, idStr;

        chanStr << i + 1 << ": ";
        idStr << i;

        destChanCombo_->append(idStr.str(), chanStr.str() +
                thUtil::basename(patch->dspFile.c_str()));

        if (first)
        {
            first = false;
            selectedDestChan_ = i;
        }
    }

    rebuilding_ = false;

    setDestChanCombo();
}

/* Selects the current channel. Under Gtk::Combo this meant rebuilding the list
   with the selection pushed to the front; now it is just set_active_id. */
void MidiMap::setDestChanCombo (void)
{
    std::ostringstream idStr;

    idStr << selectedDestChan_;

    rebuilding_ = true;
    destChanCombo_->set_active_id(idStr.str());
    rebuilding_ = false;
}

/* signal_changed replaces the per-item button-press and focus handlers. */
void MidiMap::onDestChanSelected (void)
{
    if (rebuilding_)
        return;

    Glib::ustring id = destChanCombo_->get_active_id();

    if (id.empty())
        return;

    onDestChanComboChanged(atoi(id.c_str()));
}

/* Same treatment as the channel combo. thArg pointers cannot be ids, so the
   arg name is the id and is looked back up on selection -- which is also safer
   than holding a raw thArg* in a widget across a patch reload. */
void MidiMap::fillDestArgCombo (int chan)
{
    bool first = true;

    /* Cleared before the rebuild, not after. Leaving it set meant a channel
       with no visible args kept the arg from the channel before it, and every
       control below went on editing something the combo no longer showed. */
    selectedArg_ = NULL;

    rebuilding_ = true;
    destArgCombo_->remove_all();

    if (synth_->getChannel(chan))
    {
        gthPatchManager *patchMgr = gthPatchManager::instance();
        thArgMap argList = patchMgr->getChannelArgs(chan);

        for (thArgMap::iterator i = argList.begin(); i != argList.end(); i++)
        {
            if (i->second == NULL || i->second->widgetType() == thArg::HIDE)
                continue;

            destArgCombo_->append(i->first,
                                  (i->second->label().length() > 0) ?
                                  i->second->label() : i->second->name());

            if (first)
            {
                first = false;
                selectedArg_ = i->second;
            }
        }
    }

    rebuilding_ = false;

    if (selectedArg_)
    {
        selectedMin_ = selectedArg_->min();
        selectedMax_ = selectedArg_->max();
        setDestArgCombo(chan);
    }

    /* The details and the Add button only mean anything with an arg selected.
       They used to be disabled when the list came up empty and stopped being
       so during the port, which left them live over a NULL selectedArg_. */
    set_sensitive(selectedArg_ != NULL);
}

void MidiMap::setDestArgCombo (int chan)
{
    if (selectedArg_ == NULL)
        return;

    rebuilding_ = true;
    destArgCombo_->set_active_id(selectedArg_->name());
    rebuilding_ = false;
}

void MidiMap::onDestArgSelected (void)
{
    if (rebuilding_)
        return;

    Glib::ustring id = destArgCombo_->get_active_id();

    if (id.empty())
        return;

    gthPatchManager *patchMgr = gthPatchManager::instance();
    thArgMap argList = patchMgr->getChannelArgs(selectedDestChan_);
    thArgMap::iterator i = argList.find(id);

    if (i != argList.end() && i->second)
        onDestArgComboChanged(i->second);
}

void MidiMap::populateConnections (void)
{
    gthPatchManager *patchMgr = gthPatchManager::instance();
    thMidiControllerConnection *connection;
    string instrument;
    
    connectModel_->remove_all();

    thMidiController::ConnectionMap *connectionMap =
        synth_->getMidiConnectionMap();

    for (thMidiController::ConnectionMap::iterator i =
             connectionMap->begin(); i != connectionMap->end(); i++)
    {
        connection = i->second;
        instrument = thUtil::basename(patchMgr->getPatch(
                                 connection->destChan())->filename.c_str());
        if (instrument.length() == 0)
        {
            instrument = string("Untitled");
        }
        
        std::ostringstream chanStr;
        chanStr << connection->destChan() + 1 << ": ";
        instrument = chanStr.str() + instrument;

        connectModel_->append(MidiMapRow::create(connection->chan(),
                                                connection->controller(),
                                                instrument,
                                                connection->argName()));
    }
}


void MidiMap::onChannelChanged (void)
{
    selectedChan_ = (int)channelSpinBtn_->get_value() - 1;
}

void MidiMap::onControllerChanged (void)
{
    selectedController_ = (int)controllerSpinBtn_->get_value();
}

void MidiMap::onDestChanComboChanged (int chan)
{
    fillDestArgCombo(chan);
    selectedDestChan_ = chan;
}

void MidiMap::onDestArgComboChanged (thArg *arg)
{
    selectedArg_ = arg;
    selectedMin_ = arg->min();
    selectedMax_ = arg->max();
    minSpinBtn_->set_range(selectedMin_, selectedMax_);
    maxSpinBtn_->set_range(selectedMin_, selectedMax_);
    minSpinBtn_->set_value(selectedMin_);
    maxSpinBtn_->set_value(selectedMax_);
}

void MidiMap::onMinChanged (void)
{
    selectedMin_ = minSpinBtn_->get_value();
}

void MidiMap::onMaxChanged (void)
{
    selectedMax_ = maxSpinBtn_->get_value();
}

void MidiMap::onExpToggled (void)
{
    selectedExp_ = expCheckBtn_->get_active();
}

void MidiMap::onConnectionSelected (void)
{
    /* Nothing to hit-test: the selection has already moved to the row, which
       is what this hears about. The old button-press binding had to work out
       which row had been hit for itself. */
}

void MidiMap::onConnectionMoved (void)
{
    thMidiControllerConnection *selectedConnection;

    {
        Glib::RefPtr<MidiMapRow> row =
            std::dynamic_pointer_cast<MidiMapRow>(
                connectSelection_->get_selected_item());

        /* Nothing selected -- the model was just replaced, or the user
           cleared it. There is no connection to show, so the one thing that
           only makes sense against a selected one goes insensitive and the
           editor is left alone: channel, controller, destination and details
           are also how a *new* connection is composed, and blanking them
           would take the Add button's inputs away with them. */
        if (!row)
        {
            delBtn_->set_sensitive(false);
            return;
        }

        delBtn_->set_sensitive(true);

        {
            /* Straight off the row, in the engine's own numbering. */
            selectedChan_ = row->chan();
            selectedController_ = row->controller();
            channelSpinBtn_->set_value(selectedChan_ + 1);
            controllerSpinBtn_->set_value(selectedController_);
            selectedConnection = synth_->getMidiControllerConnection(
                (unsigned char)selectedChan_,
                (unsigned int)selectedController_);

            /* The list and the controller map can disagree: a connection is
               looked up by channel and controller, and thMidiController now
               refuses an out-of-range pair rather than indexing past its
               array. Every field below reads through this pointer. */
            if (selectedConnection == NULL)
                return;

            selectedArg_ = selectedConnection->arg();
            selectedDestChan_ = selectedConnection->destChan();
            selectedExp_ = selectedConnection->scale();
            setDestChanCombo();
            setDestArgCombo(selectedDestChan_);
            selectedMin_ = selectedConnection->min();
            selectedMax_ = selectedConnection->max();
            minSpinBtn_->set_range(selectedArg_->min(),
                                   selectedArg_->max());
            maxSpinBtn_->set_range(selectedArg_->min(),
                                   selectedArg_->max());
            minSpinBtn_->set_value(selectedMin_);
            maxSpinBtn_->set_value(selectedMax_);
            expCheckBtn_->set_active(selectedExp_);
        }
    }
}

void MidiMap::onAddButton (void)
{
    thMidiControllerConnection *connection;

    connection = synth_->getMidiControllerConnection(
        (unsigned char)selectedChan_,
        (unsigned int)selectedController_);

    if (connection)
        delete connection;

    synth_->newMidiControllerConnection((unsigned char)selectedChan_,
                                        (unsigned int)selectedController_,
                                        new thMidiControllerConnection(
                                            selectedArg_, 
                                            selectedMin_, selectedMax_, 
                                            selectedExp_, selectedChan_, 
                                            selectedController_, 
                                            selectedDestChan_, 
                                            selectedArg_->label()));
    populateConnections();
}

void MidiMap::onDelButton (void)
{
    thMidiControllerConnection *connection =
    synth_->getMidiControllerConnection(selectedChan_, selectedController_);

    if (connection)
    {
        delete connection;

        synth_->newMidiControllerConnection((unsigned char)selectedChan_,
                                            (unsigned int)selectedController_,
                                            NULL);

        populateConnections();
    }
}

void MidiMap::onPatchChanged (void)
{
    populateConnections();
    setDestChanCombo();
    setDestArgCombo(selectedDestChan_);
}
