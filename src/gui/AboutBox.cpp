/* $Id: AboutBox.cpp,v 1.1 2004/09/16 07:25:39 misha Exp $ */
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

gchar *get_credits()
{
   static gchar credits[] =
"\
       Main Programming: Leif M. Ames \n\
                         <ink@bespin.org>\n\
                         Misha Nasledov \n\
                         <misha@nasledov.com>\n\
                         Joshua Kwan\n\
                         <joshk@triplehelix.org>\n\
                         Aaron Lehmann\n\
                         <aaronl@vitelus.com>\n\n\
";
   return (gchar*) credits;
}

AboutBox::AboutBox (void)
{
	set_size_request (482, 450);
	set_title("About thinksynth");

	realize();

	fixed = manage(new Gtk::Fixed);
	add(*fixed);

	btnClose = manage(new Gtk::Button("Close"));
	fixed->put(*btnClose, 384, 393);
	btnClose->set_size_request(88, 36);
	GTK_WIDGET_SET_FLAGS(btnClose->gobj(), GTK_CAN_DEFAULT);
	btnClose->grab_focus();
	btnClose->grab_default();

	notebook = manage(new Gtk::Notebook);
	fixed->put(*notebook, 8, 8);
	notebook->set_size_request(466, 382);

	fixed1 = manage(new Gtk::Fixed);
	notebook->append_page(*fixed1, "Credits");

	frame = manage(new Gtk::Frame);
	fixed1->put(*frame, 32, 12);
	frame->set_size_request(400, 200);
	frame->set_shadow_type(Gtk::SHADOW_OUT);

//	icon = Gdk::Pixmap::create_from_xpm_d

	lblTitle = manage(new Gtk::Label(PACKAGE_STRING));
	fixed1->put(*lblTitle, 8, 175);
	lblTitle->set_size_request(448, 16);

	lblCopyright = manage(new Gtk::Label("Copyright (C) 2004"));
	fixed1->put(*lblCopyright, 8, 191);
	lblCopyright->set_size_request(448, 16);

	scrolledWindow = manage(new Gtk::ScrolledWindow);
	fixed1->put(*scrolledWindow, 12, 220);
	scrolledWindow->set_size_request(436, 100);
	scrolledWindow->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);

	txtCredits = manage(new Gtk::TextView);
	scrolledWindow->add(*txtCredits);
	txtBuf = txtCredits->get_buffer();
	Glib::RefPtr<Gtk::TextBuffer::Tag> fontTag = txtBuf->create_tag("font");
	fontTag->property_font() = "monospace 9";
	txtBuf->insert_with_tag(txtBuf->begin(), get_credits(), fontTag);

	adj = scrolledWindow->get_vadjustment();
	adj->set_value(0);

	show_all_children();
}

AboutBox::~AboutBox (void)
{
}
