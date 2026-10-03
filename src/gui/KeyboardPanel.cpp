/*
 * Copyright (C) 2004-2026 The thinksynth authors
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <gtkmm.h>

#include "think.h"

#include "Keyboard.h"
#include "KeyboardPanel.h"
#include "gthSignal.h"

KeyboardPanel::KeyboardPanel (thSynth *synth)
    : Gtk::Box(Gtk::Orientation::HORIZONTAL)
{
    synth_ = synth;

    ctrlTable_ = manage(new Gtk::Grid);
    keyboard_ = manage(new Keyboard);
    chanLbl_ = manage(new Gtk::Label("Channel"));
    transLbl_ = manage(new Gtk::Label("Transpose"));
    resetBtn_ = manage(new Gtk::Button("Reset"));

    /* The controls in a column at the left and the keys across the rest,
       so the strip costs the window the keys' height and no more. */
    ctrlTable_->set_valign(Gtk::Align::CENTER);
    append(*ctrlTable_);

    /* The keys scale to what they are given (Keyboard::onDraw); what they
       asked for as a window of their own, 825px, would widen the main
       window past its default. */
    keyboard_->set_size_request(400, 88);
    keyboard_->set_hexpand(true);
    append(*keyboard_);

    /* Over the controls only: over the keys a wheel is a scroll of the
       window, not a change of channel. */
    scroll_ = Gtk::EventControllerScroll::create();
    scroll_->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL);
    scroll_->signal_scroll().connect(
        sigc::mem_fun(*this, &KeyboardPanel::onScroll), false);
    ctrlTable_->add_controller(scroll_);

    /* gtkmm-3: Adjustment is refcounted with a protected constructor, so it is
       created through the factory and held by RefPtr rather than manage()d.
       That also retires the double free the old destructor had here -- it
       delete'd two Gtk::manage()d adjustments the SpinButtons already owned. */
    chanVal_ = Gtk::Adjustment::create(1, 1, synth_->midiChanCount());
    chanBtn_ = manage(new Gtk::SpinButton(chanVal_));

    transVal_ = Gtk::Adjustment::create(0, -72, 72);
    transBtn_ = manage(new Gtk::SpinButton(transVal_));

    chanLbl_->set_margin_start(5);
    chanLbl_->set_margin_end(5);
    chanLbl_->set_margin_top(5);
    chanLbl_->set_margin_bottom(5);
    ctrlTable_->attach(*chanLbl_, 0, 0, 1, 1);
    chanBtn_->set_margin_start(5);
    chanBtn_->set_margin_end(5);
    chanBtn_->set_margin_top(5);
    chanBtn_->set_margin_bottom(5);
    ctrlTable_->attach(*chanBtn_, 1, 0, 1, 1);

    transLbl_->set_margin_start(5);
    transLbl_->set_margin_end(5);
    transLbl_->set_margin_top(5);
    transLbl_->set_margin_bottom(5);
    transLbl_->set_xalign(0.0);
    chanLbl_->set_xalign(0.0);
    ctrlTable_->attach(*transLbl_, 0, 1, 1, 1);
    transBtn_->set_margin_start(5);
    transBtn_->set_margin_end(5);
    transBtn_->set_margin_top(5);
    transBtn_->set_margin_bottom(5);
    ctrlTable_->attach(*transBtn_, 1, 1, 1, 1);

    resetBtn_->set_margin_start(5);
    resetBtn_->set_margin_end(5);
    resetBtn_->set_margin_top(5);
    resetBtn_->set_margin_bottom(5);
    ctrlTable_->attach(*resetBtn_, 0, 2, 2, 1);

    chanVal_->signal_value_changed().connect(
        sigc::mem_fun(*this, &KeyboardPanel::changeChannel));

    transVal_->signal_value_changed().connect(
        sigc::mem_fun(*this, &KeyboardPanel::changeTranspose));

    keyboard_->signal_note_on().connect(
        sigc::mem_fun(*this, &KeyboardPanel::eventNoteOn));

    keyboard_->signal_note_off().connect(
        sigc::mem_fun(*this, &KeyboardPanel::eventNoteOff));

    keyboard_->signal_channel_changed().connect(
        sigc::mem_fun(*this, &KeyboardPanel::eventChannelChanged));

    keyboard_->signal_transpose_changed().connect(
        sigc::mem_fun(*this, &KeyboardPanel::eventTransposeChanged));

    chanBtn_->set_can_focus(false);
    transBtn_->set_can_focus(false);
    resetBtn_->set_can_focus(false);

    resetBtn_->signal_clicked().connect(
        sigc::mem_fun(*this, &KeyboardPanel::keyboardReset));

    m_sigNoteOn.connect(sigc::mem_fun(*this,
                                      &KeyboardPanel::synthEventNoteOn));

    m_sigNoteOff.connect(
        sigc::mem_fun(*this, &KeyboardPanel::synthEventNoteOff));

    m_sigNoteClear.connect(
        sigc::mem_fun(*this, &KeyboardPanel::keyboardResetKeys));

/*  This has the undesired effect of also cutting off MIDI notes!
    signal_focus_out_event().connect(
        sigc::mem_fun(*this, &KeyboardPanel::keyboardReset)); */
}

void KeyboardPanel::keyboardReset (void)
{
    synth_->clearAll();

    /* Everything is silent now, held keys and MIDI notes whose off may
       never come: what draws them as down is told so. */
    m_sigNoteClear();
}

void KeyboardPanel::keyboardResetKeys (void)
{
    keyboard_->resetKeys();
}

void KeyboardPanel::setChannel (int chan)
{
    if (chan >= 0 && chan < synth_->midiChanCount())
        chanVal_->set_value(chan + 1);
}

KeyboardPanel::~KeyboardPanel (void)
{
    /* Nothing to free here: chanVal_ and transVal_ are refcounted and held
       by the spin buttons, which die with the panel. */
}

/* these are Keyboard widget-originated events */
void KeyboardPanel::eventNoteOn (int chan, int note, float veloc)
{
    synth_->addNote(chan, note, veloc);
    m_sigKbdNoteOn(chan, note, veloc);
}

void KeyboardPanel::eventNoteOff (int chan, int note)
{
    synth_->delNote(chan, note);
    m_sigKbdNoteOff(chan, note);
}

void KeyboardPanel::eventChannelChanged (int chan)
{
    chanVal_->set_value(chan+1);
}

void KeyboardPanel::eventTransposeChanged (int trans)
{
    transVal_->set_value(trans);
}

/* these are synthesizer engine thread-originated events, so the appropriate
   multi-threaded precautions must be taken here .. */
void KeyboardPanel::synthEventNoteOn (int chan, float note, float veloc)
{
    if (chan != keyboard_->GetChannel())
         return;

     kbMutex_.lock();
    keyboard_->SetNote((int)note, true);
    kbMutex_.unlock();
}

void KeyboardPanel::synthEventNoteOff (int chan, float note)
{
    if (chan != keyboard_->GetChannel())
        return;

    kbMutex_.lock();
    keyboard_->SetNote((int)note, false);
    kbMutex_.unlock();
}

void KeyboardPanel::changeChannel (void)
{
    kbMutex_.lock();
    /* the keyboard widget takes the real channel value */
    keyboard_->SetChannel((int)chanVal_->get_value()-1);
    kbMutex_.unlock();
}

void KeyboardPanel::changeTranspose (void)
{
    kbMutex_.lock();
    keyboard_->SetTranspose((int)transVal_->get_value());
    kbMutex_.unlock();
}

/* dy is negative upwards, and a smooth device reports fractions of a step.
   Only the sign is wanted here: one channel per notch, as before. */
bool KeyboardPanel::onScroll (double dx, double dy)
{
    (void)dx;

    if (dy == 0.0)
        return false;

    float channel = chanVal_->get_value() + (dy < 0.0 ? 1 : -1);

    if ((channel < 1) || (channel > synth_->midiChanCount()))
        return true;

    chanVal_->set_value(channel);

    return true;
}
