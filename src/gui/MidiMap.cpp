/*
 * Copyright (C) 2004-2014 Metaphonic Labs
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

    mainVBox_ = manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL));
    inputVBox_ = manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0));
    inputVBox_->set_size_request(700, 140);
    newConnectionFrame_ = manage(new Gtk::Frame("Connection Source"));
    destinationFrame_ = manage(new Gtk::Frame("Connection Destination"));
    detailsFrame_ = manage(new Gtk::Frame("Connection Details"));
    connectFrame_ = manage(new Gtk::Frame("Connections"));
    srcDestHBox_ = manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 3));
    srcDestHBox_->set_homogeneous(true);
    newConnectionHBox_ = manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 0));
    destinationHBox_ = manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 4));
    destinationHBox_->set_homogeneous(true);
    detailsHBox_ = manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 0));
    detailsHBox_->set_homogeneous(true);
    buttonsHBox_ = manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 0));
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
    buttonsHBox_->pack_start(*addBtn_, Gtk::PACK_EXPAND_WIDGET);
    buttonsHBox_->pack_start(*delBtn_, Gtk::PACK_EXPAND_WIDGET);

    destChanCombo_ = manage(new Gtk::ComboBoxText);
    destChanCombo_->signal_changed().connect(
        sigc::mem_fun(*this, &MidiMap::onDestChanSelected));
    fillDestChanCombo();

    destArgCombo_ = manage(new Gtk::ComboBoxText);
    destArgCombo_->signal_changed().connect(
        sigc::mem_fun(*this, &MidiMap::onDestArgSelected));
    fillDestArgCombo(selectedDestChan_);

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
    detailsHBox_->pack_start(*expLbl_, Gtk::PACK_EXPAND_WIDGET);
    detailsHBox_->pack_start(*expCheckBtn_, Gtk::PACK_SHRINK);
    detailsFrame_->add(*detailsHBox_);

    connectScroll_.add(connectView_);
    connectScroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    connectScroll_.set_size_request(700, 128);

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
    inputVBox_->pack_start(*buttonsHBox_, Gtk::PACK_EXPAND_WIDGET);

    mainVBox_->pack_start(*connectFrame_, Gtk::PACK_EXPAND_WIDGET);
    mainVBox_->pack_start(*inputVBox_, Gtk::PACK_SHRINK);

    show_all_children();

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
                thUtil::basename((char*)patch->dspFile.c_str()));

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

    onDestChanComboChanged(NULL, atoi(id.c_str()));
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
        onDestArgComboChanged(NULL, i->second);
}

void MidiMap::populateConnections (void)
{
    gthPatchManager *patchMgr = gthPatchManager::instance();
    thMidiControllerConnection *connection;
    string instrument;
    
    connectModel_->clear();

    thMidiController::ConnectionMap *connectionMap =
        synth_->getMidiConnectionMap();
    
    for (thMidiController::ConnectionMap::iterator i =
             connectionMap->begin(); i != connectionMap->end(); i++)
    {
        Gtk::TreeModel::Row row = *(connectModel_->append());
        connection = i->second;
        instrument = thUtil::basename((char*)patchMgr->getPatch(
                                 connection->destChan())->filename.c_str());
        if (instrument.length() == 0)
        {
            instrument = string("Untitled");
        }
        
        std::ostringstream chanStr;
        chanStr << connection->destChan() + 1 << ": ";
        instrument = chanStr.str() + instrument;

        row[connectViewCols_.midiChan] = connection->chan() + 1;
        row[connectViewCols_.midiController] = connection->controller();
        row[connectViewCols_.instrument] = instrument;
        row[connectViewCols_.argName] = connection->argName();
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

bool MidiMap::onDestChanComboChanged (GdkEventButton* b, int chan)
{
    fillDestArgCombo(chan);
    selectedDestChan_ = chan;
    return true;
}

bool MidiMap::onDestArgComboChanged (GdkEventButton* b, thArg *arg)
{
    selectedArg_ = arg;
    selectedMin_ = arg->min();
    selectedMax_ = arg->max();
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

void MidiMap::onExpToggled (void)
{
    selectedExp_ = expCheckBtn_->get_active();
}

void MidiMap::onConnectionSelected (GdkEventButton *b)
{
    if (b && b->type == GDK_BUTTON_PRESS)
    {
        Glib::RefPtr<Gtk::TreeView::Selection> refSelection = 
            connectView_.get_selection();
        
        if (refSelection)
        {
            Gtk::TreeModel::iterator iter;
            Gtk::TreeModel::Path path;
            Gtk::TreeViewColumn *col;
            int cell_x, cell_y; 

            if (connectView_.get_path_at_pos((int)b->x, (int)b->y, path, col, 
                                      cell_x, cell_y))
                refSelection->select(path);
        }
    }
}

void MidiMap::onConnectionMoved (void)
{
    Glib::RefPtr<Gtk::TreeView::Selection> refSelection = 
        connectView_.get_selection();
    thMidiControllerConnection *selectedConnection;

    if (refSelection)
    {
        Gtk::TreeModel::iterator iter;
        iter = refSelection->get_selected();
        
        if (iter)
        {
            selectedChan_ = (*iter)[connectViewCols_.midiChan] - 1;
            selectedController_ = (*iter)[connectViewCols_.midiController];
            channelSpinBtn_->set_value(selectedChan_ + 1);
            controllerSpinBtn_->set_value(selectedController_);
            selectedConnection = synth_->getMidiControllerConnection(
                (unsigned char)selectedChan_,
                (unsigned int)selectedController_);

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
