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

KeyboardWindow::KeyboardWindow (thSynth *argsynth)
	: ctrlFrame ("Keyboard Control"), 
	  chanLbl("Channel"),
	  transLbl("Transpose"),
	  resetBtn("Reset")
{
	synth = argsynth;

	set_title("thinksynth - Keyboard");

	add(vbox);

	vbox.pack_start(ctrlFrame, Gtk::PACK_SHRINK, 5);
	vbox.pack_start(keyboard);

	ctrlFrame.add(ctrlTable);

	chanVal = new Gtk::Adjustment(1, 1, synth->GetChannelCount());
	chanBtn = new Gtk::SpinButton(*chanVal);

	transVal = new Gtk::Adjustment(0, -72, 72);
	transBtn = new Gtk::SpinButton(*transVal);

	ctrlTable.attach(chanLbl, 0, 1, 0, 1, Gtk::SHRINK, Gtk::SHRINK, 5, 5);
	ctrlTable.attach(*chanBtn, 1, 2, 0, 1, Gtk::SHRINK, Gtk::SHRINK, 5, 5);

	ctrlTable.attach(transLbl, 2, 3, 0, 1, Gtk::SHRINK, Gtk::SHRINK, 5, 5);
	ctrlTable.attach(*transBtn, 3, 4, 0, 1, Gtk::SHRINK, Gtk::SHRINK, 5, 5);

	ctrlTable.attach(resetBtn, 4, 5, 0, 1, Gtk::SHRINK, Gtk::SHRINK, 5, 5);

	chanVal->signal_value_changed().connect(
		sigc::mem_fun(*this, &KeyboardWindow::changeChannel));

	transVal->signal_value_changed().connect(
		sigc::mem_fun(*this, &KeyboardWindow::changeTranspose));

	keyboard.signal_note_on().connect(
		sigc::mem_fun(*this, &KeyboardWindow::eventNoteOn));

	keyboard.signal_note_off().connect(
		sigc::mem_fun(*this, &KeyboardWindow::eventNoteOff));

	keyboard.signal_channel_changed().connect(
		sigc::mem_fun(*this, &KeyboardWindow::eventChannelChanged));

	keyboard.signal_transpose_changed().connect(
		sigc::mem_fun(*this, &KeyboardWindow::eventTransposeChanged));

	chanBtn->unset_flags(Gtk::CAN_FOCUS);
	transBtn->unset_flags(Gtk::CAN_FOCUS);
	resetBtn.unset_flags(Gtk::CAN_FOCUS);

	m_sigNoteOn.connect(sigc::mem_fun(*this,
								   &KeyboardWindow::synthEventNoteOn));

	m_sigNoteOff.connect(
		sigc::mem_fun(*this, &KeyboardWindow::synthEventNoteOff));

	m_sigNoteClear.connect(
		sigc::mem_fun(*this, &KeyboardWindow::keyboardResetKeys));

	resetBtn.signal_clicked().connect(
		sigc::mem_fun(*this, &KeyboardWindow::keyboardReset));

/*  This has the undesired effect of also cutting off MIDI notes!
	signal_focus_out_event().connect(
		sigc::mem_fun(*this, &KeyboardWindow::keyboardReset)); */
}

void KeyboardWindow::keyboardReset (void)
{
	synth->ClearAll();
	/* keyboardResetKeys is called somewhere along the way */
}

void KeyboardWindow::keyboardResetKeys (void)
{
	keyboard.resetKeys();
}

KeyboardWindow::~KeyboardWindow (void)
{
	/* free dynamically-allocated widgets */
	delete chanVal;
	delete transVal;
}

/* these are Keyboard widget-originated events */
void KeyboardWindow::eventNoteOn (int chan, int note, float veloc)
{
	synth->AddNote(chan, note, veloc);
}

void KeyboardWindow::eventNoteOff (int chan, int note)
{
	synth->DelNote(chan, note);
}

void KeyboardWindow::eventChannelChanged (int chan)
{
	chanVal->set_value(chan+1);
}

void KeyboardWindow::eventTransposeChanged (int trans)
{
	transVal->set_value(trans);
}

/* these are synthesizer engine thread-originated events, so the appropriate
   multi-threaded precautions must be taken here .. */
void KeyboardWindow::synthEventNoteOn (int chan, float note, float veloc)
{
	if(chan != keyboard.GetChannel())
 		return;

 	kbMutex.lock();
	keyboard.SetNote((int)note, true);
	kbMutex.unlock();
}

void KeyboardWindow::synthEventNoteOff (int chan, float note)
{
	if(chan != keyboard.GetChannel())
		return;

	kbMutex.lock();
	keyboard.SetNote((int)note, false);
	kbMutex.unlock();
}

void KeyboardWindow::changeChannel (void)
{
	kbMutex.lock();
	/* the keyboard widget takes the real channel value */
	keyboard.SetChannel((int)chanVal->get_value()-1);
	kbMutex.unlock();
}

void KeyboardWindow::changeTranspose (void)
{
	kbMutex.lock();
	keyboard.SetTranspose((int)transVal->get_value());
	kbMutex.unlock();
}

bool KeyboardWindow::on_scroll_event (GdkEventScroll *s)
{
	float channel = chanVal->get_value();

	channel += (s->direction == GDK_SCROLL_UP ? 1 : -1);

	if ((channel < 1) || (channel > synth->GetChannelCount()))
		return true;

	chanVal->set_value(channel);

	return true;
}
