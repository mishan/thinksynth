/* $Id: AboutBox.cpp,v 1.5 2004/09/17 01:39:27 misha Exp $ */
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

#include "AboutBox.h"
#include "thinksynth.xpm"

AboutBox::AboutBox (void)
{
	set_size_request (482, 430);
	set_title("About thinksynth");

	realize();

	fixed = manage(new Gtk::Fixed);
	add(*fixed);

	btnClose = manage(new Gtk::Button("Close"));
	btnClose->signal_clicked().connect(SigC::slot(*this,
												  &AboutBox::onCloseButton));
	fixed->put(*btnClose, 384, 383);
	btnClose->set_size_request(88, 36);
	GTK_WIDGET_SET_FLAGS(btnClose->gobj(), GTK_CAN_DEFAULT);
	btnClose->grab_focus();
	btnClose->grab_default();

	notebook = manage(new Gtk::Notebook);
	fixed->put(*notebook, 8, 8);
	notebook->set_size_request(466, 362);

	fixed1 = manage(new Gtk::Fixed);
	notebook->append_page(*fixed1, "Credits");

	frame = manage(new Gtk::Frame);
	fixed1->put(*frame, 32, 12);
	frame->set_border_width(0);
	frame->set_size_request(415, 130);
	frame->set_shadow_type(Gtk::SHADOW_OUT);

	pixmap = Gdk::Pixmap::create_from_xpm(get_colormap(), mask, thinksynth);
	logo = new Gtk::Image(pixmap, mask);
	frame->add(*logo);

	lblTitle = manage(new Gtk::Label(PACKAGE_STRING));
	fixed1->put(*lblTitle, 8, 150);
	lblTitle->set_size_request(448, 20);

	lblCopyright = manage(new Gtk::Label("Copyright (C) 2004"));
	fixed1->put(*lblCopyright, 8, 166);
	lblCopyright->set_size_request(448, 20);

	scrolledWindow = manage(new Gtk::ScrolledWindow);
	fixed1->put(*scrolledWindow, 12, 200);
	scrolledWindow->set_size_request(436, 120);
	scrolledWindow->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);

	txtCredits = manage(new Gtk::TextView);
	txtCredits->set_editable(false);
	scrolledWindow->add(*txtCredits);
	txtBuf = txtCredits->get_buffer();
	Glib::RefPtr<Gtk::TextBuffer::Tag> fontTag = txtBuf->create_tag("font");
	fontTag->property_font() = CREDITS_FONT;
	txtBuf->insert_with_tag(txtBuf->begin(), CREDITS_TEXT, fontTag);

	adj = scrolledWindow->get_vadjustment();
	adj->set_value(0);

	show_all_children();
}

AboutBox::~AboutBox (void)
{
}

void AboutBox::onCloseButton (void)
{
	hide ();
}
