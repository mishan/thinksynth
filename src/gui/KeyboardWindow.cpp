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

	add(vbox_);

	ctrlTable_ = manage(new Gtk::Table);
	keyboard_ = manage(new Keyboard);
	ctrlFrame_ = manage(new Gtk::Frame("Keyboard Control"));
	chanLbl_ = manage(new Gtk::Label("Channel"));
	transLbl_ = manage(new Gtk::Label("Transpose"));
	resetBtn_ = manage(new Gtk::Button("Reset"));

	vbox_.pack_start(*ctrlFrame_, Gtk::PACK_SHRINK, 5);
	vbox_.pack_start(*keyboard_);

	ctrlFrame_->add(*ctrlTable_);

	chanVal_ = manage(new Gtk::Adjustment(1, 1, synth_->GetChannelCount()));
	chanBtn_ = manage(new Gtk::SpinButton(*chanVal_));

	transVal_ = manage(new Gtk::Adjustment(0, -72, 72));
	transBtn_ = manage(new Gtk::SpinButton(*transVal_));

	ctrlTable_->attach(*chanLbl_, 0, 1, 0, 1, Gtk::SHRINK, Gtk::SHRINK, 5, 5);
	ctrlTable_->attach(*chanBtn_, 1, 2, 0, 1, Gtk::SHRINK, Gtk::SHRINK, 5, 5);

	ctrlTable_->attach(*transLbl_, 2, 3, 0, 1, Gtk::SHRINK, Gtk::SHRINK, 5, 5);
	ctrlTable_->attach(*transBtn_, 3, 4, 0, 1, Gtk::SHRINK, Gtk::SHRINK, 5, 5);

	ctrlTable_->attach(*resetBtn_, 4, 5, 0, 1, Gtk::SHRINK, Gtk::SHRINK, 5, 5);

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

	chanBtn_->unset_flags(Gtk::CAN_FOCUS);
	transBtn_->unset_flags(Gtk::CAN_FOCUS);
	resetBtn_->unset_flags(Gtk::CAN_FOCUS);

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
	synth_->ClearAll();
	/* keyboardResetKeys is called somewhere along the way */
}

void KeyboardWindow::keyboardResetKeys (void)
{
	keyboard_->resetKeys();
}

KeyboardWindow::~KeyboardWindow (void)
{
	/* free dynamically-allocated widgets */
	delete chanVal_;
	delete transVal_;
}

/* these are Keyboard widget-originated events */
void KeyboardWindow::eventNoteOn (int chan, int note, float veloc)
{
	synth_->AddNote(chan, note, veloc);
}

void KeyboardWindow::eventNoteOff (int chan, int note)
{
	synth_->DelNote(chan, note);
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
	if(chan != keyboard_->GetChannel())
 		return;

 	kbMutex_.lock();
	keyboard_->SetNote((int)note, true);
	kbMutex_.unlock();
}

void KeyboardWindow::synthEventNoteOff (int chan, float note)
{
	if(chan != keyboard_->GetChannel())
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

bool KeyboardWindow::on_scroll_event (GdkEventScroll *s)
{
	float channel = chanVal_->get_value();

	channel += (s->direction == GDK_SCROLL_UP ? 1 : -1);

	if ((channel < 1) || (channel > synth_->GetChannelCount()))
		return true;

	chanVal_->set_value(channel);

	return true;
}
