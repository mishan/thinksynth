/* $Id: KeyboardWindow.cpp,v 1.22 2004/04/04 10:41:40 misha Exp $ */

#include "config.h"
#include "think.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <gtkmm.h>

#include "thArg.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thMod.h"
#include "thMidiNote.h"
#include "thMidiChan.h"
#include "thSynth.h"

#include "Keyboard.h"
#include "KeyboardWindow.h"
#include "signal.h"

KeyboardWindow::KeyboardWindow (thSynth *argsynth)
	: ctrlFrame ("Keyboard Control"), 
	  chanLbl("Channel"),
	  transLbl("Transpose")
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

	chanVal->signal_value_changed().connect(
		SigC::slot(*this, &KeyboardWindow::changeChannel));

	transVal->signal_value_changed().connect(
		SigC::slot(*this, &KeyboardWindow::changeTranspose));

	keyboard.signal_note_on().connect(
		SigC::slot(*this, &KeyboardWindow::eventNoteOn));

	keyboard.signal_note_off().connect(
		SigC::slot(*this, &KeyboardWindow::eventNoteOff));

	keyboard.signal_channel_changed().connect(
		SigC::slot(*this, &KeyboardWindow::eventChannelChanged));

	keyboard.signal_transpose_changed().connect(
		SigC::slot(*this, &KeyboardWindow::eventTransposeChanged));

	GTK_WIDGET_UNSET_FLAGS(GTK_WIDGET(chanBtn->gobj()), GTK_CAN_FOCUS);
	GTK_WIDGET_UNSET_FLAGS(GTK_WIDGET(transBtn->gobj()), GTK_CAN_FOCUS);

	m_sigNoteOn.connect(SigC::slot(*this, &KeyboardWindow::synthEventNoteOn));
	m_sigNoteOff.connect(
		SigC::slot(*this, &KeyboardWindow::synthEventNoteOff));
}

KeyboardWindow::~KeyboardWindow (void)
{
	delete chanVal;
	delete transVal;
}

void KeyboardWindow::eventNoteOn (int chan, int note, float veloc)
{
	synth->AddNote(chan, note, veloc);
}

void KeyboardWindow::eventNoteOff (int chan, int note)
{
	synth->DelNote(chan, note);
}

void KeyboardWindow::synthEventNoteOn (int chan, float note, float veloc)
{
//	keyboard.signal_note_on().emit(chan, (int)note, veloc);
	keyboard.SetNote((int)note, true);
}

void KeyboardWindow::synthEventNoteOff (int chan, float note)
{
//	keyboard.signal_note_off().emit(chan, (int)note);
	keyboard.SetNote((int)note, false);
}

void KeyboardWindow::eventChannelChanged (int chan)
{
	chanVal->set_value(chan+1);
}

void KeyboardWindow::eventTransposeChanged (int trans)
{
	transVal->set_value(trans);
}

void KeyboardWindow::changeChannel (void)
{
	/* the keyboard widget takes the real channel value */
	keyboard.SetChannel((int)chanVal->get_value()-1);
}

void KeyboardWindow::changeTranspose (void)
{
	keyboard.SetTranspose((int)transVal->get_value());
}
