/* $Id: KeyboardWindow.cpp,v 1.17 2004/04/02 11:33:11 misha Exp $ */

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


KeyboardWindow::KeyboardWindow (thSynth *argsynth)
	: ctrlFrame ("Keyboard Control"), 
	  chanLbl("Channel"),
	  transLbl("Transpose")
{
	synth = argsynth;

	set_title("thinksynth - Keyboard");

	add(vbox);

	vbox.pack_start(ctrlFrame, Gtk::PACK_SHRINK, 5);

	/* we must realize the widgets before we do some things .. */
	realize();
	keyboard = new Keyboard(&vbox, synth);

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
}

KeyboardWindow::~KeyboardWindow (void)
{
//	hide ();

//  gdk_key_repeat_enable ()
	delete keyboard;
}

void KeyboardWindow::changeChannel (void)
{
	/* the keyboard widget takes the real channel value */
	keyboard->SetChannel((int)chanVal->get_value()-1);
}

void KeyboardWindow::changeTranspose (void)
{
	keyboard->SetTranspose((int)transVal->get_value());
}
