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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <gtkmm.h>

#include "think.h"

#include "Keyboard.h"
#include "KeyboardWindow.h"
#include "gthSignal.h"

KeyboardWindow::KeyboardWindow (thSynth *synth)
{
    synth_ = synth;

    set_title("thinksynth - Keyboard");

    set_child(vbox_);

    scroll_ = Gtk::EventControllerScroll::create();
    scroll_->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL);
    scroll_->signal_scroll().connect(
        sigc::mem_fun(*this, &KeyboardWindow::onScroll), false);
    add_controller(scroll_);

    ctrlTable_ = manage(new Gtk::Grid);
    keyboard_ = manage(new Keyboard);
    ctrlFrame_ = manage(new Gtk::Frame("Keyboard Control"));
    chanLbl_ = manage(new Gtk::Label("Channel"));
    transLbl_ = manage(new Gtk::Label("Transpose"));
    resetBtn_ = manage(new Gtk::Button("Reset"));

    vbox_.append(*ctrlFrame_);
    keyboard_->set_vexpand(true);
    vbox_.append(*keyboard_);

    ctrlFrame_->set_child(*ctrlTable_);

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
    ctrlTable_->attach(*transLbl_, 2, 0, 1, 1);
    transBtn_->set_margin_start(5);
    transBtn_->set_margin_end(5);
    transBtn_->set_margin_top(5);
    transBtn_->set_margin_bottom(5);
    ctrlTable_->attach(*transBtn_, 3, 0, 1, 1);

    resetBtn_->set_margin_start(5);
    resetBtn_->set_margin_end(5);
    resetBtn_->set_margin_top(5);
    resetBtn_->set_margin_bottom(5);
    ctrlTable_->attach(*resetBtn_, 4, 0, 1, 1);

    chanVal_->signal_value_changed().connect(
        sigc::mem_fun(*this, &KeyboardWindow::changeChannel));

    transVal_->signal_value_changed().connect(
        sigc::mem_fun(*this, &KeyboardWindow::changeTranspose));

    keyboard_->signal_note_on().connect(
        sigc::mem_fun(*this, &KeyboardWindow::eventNoteOn));

    keyboard_->signal_note_off().connect(
        sigc::mem_fun(*this, &KeyboardWindow::eventNoteOff));

    keyboard_->signal_channel_changed().connect(
        sigc::mem_fun(*this, &KeyboardWindow::eventChannelChanged));

    keyboard_->signal_transpose_changed().connect(
        sigc::mem_fun(*this, &KeyboardWindow::eventTransposeChanged));

    chanBtn_->set_can_focus(false);
    transBtn_->set_can_focus(false);
    resetBtn_->set_can_focus(false);

    resetBtn_->signal_clicked().connect(
        sigc::mem_fun(*this, &KeyboardWindow::keyboardReset));

    m_sigNoteOn.connect(sigc::mem_fun(*this,
                                      &KeyboardWindow::synthEventNoteOn));

    m_sigNoteOff.connect(
        sigc::mem_fun(*this, &KeyboardWindow::synthEventNoteOff));

    m_sigNoteClear.connect(
        sigc::mem_fun(*this, &KeyboardWindow::keyboardResetKeys));

/*  This has the undesired effect of also cutting off MIDI notes!
    signal_focus_out_event().connect(
        sigc::mem_fun(*this, &KeyboardWindow::keyboardReset)); */
}

void KeyboardWindow::keyboardReset (void)
{
    synth_->clearAll();
    /* keyboardResetKeys is called somewhere along the way */
}

void KeyboardWindow::keyboardResetKeys (void)
{
    keyboard_->resetKeys();
}

KeyboardWindow::~KeyboardWindow (void)
{
    /* Nothing to free here.
     *
     * chanVal_ and transVal_ are created with Gtk::manage(), which hands
     * ownership to the SpinButtons that hold them; those die with the window.
     * Deleting them again was a double free. MainSynthWindow destroys this
     * window every time the keyboard is closed (onKeyboardHide) and builds a
     * fresh one when it is reopened, so the heap got corrupted on the first
     * close -- which is what left the channel spinner reading back nonsense
     * like -733809408, the new adjustment having been handed memory the
     * previous window already freed twice.
     */
}

/* these are Keyboard widget-originated events */
void KeyboardWindow::eventNoteOn (int chan, int note, float veloc)
{
    synth_->addNote(chan, note, veloc);
}

void KeyboardWindow::eventNoteOff (int chan, int note)
{
    synth_->delNote(chan, note);
}

void KeyboardWindow::eventChannelChanged (int chan)
{
    chanVal_->set_value(chan+1);
}

void KeyboardWindow::eventTransposeChanged (int trans)
{
    transVal_->set_value(trans);
}

/* these are synthesizer engine thread-originated events, so the appropriate
   multi-threaded precautions must be taken here .. */
void KeyboardWindow::synthEventNoteOn (int chan, float note, float veloc)
{
    if (chan != keyboard_->GetChannel())
         return;

     kbMutex_.lock();
    keyboard_->SetNote((int)note, true);
    kbMutex_.unlock();
}

void KeyboardWindow::synthEventNoteOff (int chan, float note)
{
    if (chan != keyboard_->GetChannel())
        return;

    kbMutex_.lock();
    keyboard_->SetNote((int)note, false);
    kbMutex_.unlock();
}

void KeyboardWindow::changeChannel (void)
{
    kbMutex_.lock();
    /* the keyboard widget takes the real channel value */
    keyboard_->SetChannel((int)chanVal_->get_value()-1);
    kbMutex_.unlock();
}

void KeyboardWindow::changeTranspose (void)
{
    kbMutex_.lock();
    keyboard_->SetTranspose((int)transVal_->get_value());
    kbMutex_.unlock();
}

/* dy is negative upwards, and a smooth device reports fractions of a step.
   Only the sign is wanted here: one channel per notch, as before. */
bool KeyboardWindow::onScroll (double dx, double dy)
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
