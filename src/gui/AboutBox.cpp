/* $Id: AboutBox.cpp,v 1.7 2004/09/17 04:56:16 joshk Exp $ */
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

static const char* authors [] = {
	"Leif M. Ames", "Misha Nasledov", "Joshua Kwan", "Aaron Lehmann", 0
};

static const char* emails [] = {
	"ink@bespin.org", "misha@nasledov.com", "joshk@triplehelix.org",
	"aaronl@vitelus.com", 0
};

AboutBox::AboutBox (void)
{
	const char **a = authors, **e = emails;

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
	btnClose->set_flags(Gtk::CAN_DEFAULT);
	btnClose->grab_focus();
	btnClose->grab_default();

	notebook = manage(new Gtk::Notebook);
	fixed->put(*notebook, 8, 8);
	notebook->set_size_request(466, 362);

	frame = manage(new Gtk::Frame);
	frame->set_border_width(0);
	frame->set_size_request(415, 135);
	frame->set_shadow_type(Gtk::SHADOW_OUT);

	pixmap = Gdk::Pixmap::create_from_xpm(get_colormap(), mask, thinksynth);
	logo = new Gtk::Image(pixmap, mask);
	frame->add(*logo);

	/* Hack to get it to shrink down to our size */
	framebox = manage(new Gtk::HBox);
	framebox->pack_start(*frame, true, false);
	
	vbmaster = manage (new Gtk::VBox);
	
	/* Too bad that Gtk::Labels lose their alignment if the label has >1
	 * line in it. */
#if 0
	header = manage(new Gtk::Label(
		  "Version " PACKAGE_VERSION "\n"
		  "Copyright (C) 2004 Metaphonic Labs\n\n"
		  "Metaphonic Labs is...", Gtk::ALIGN_CENTER));
#endif
	txtVersion = manage(new Gtk::Label("Version " PACKAGE_VERSION, 0.5));
	txtCopyright = manage(new Gtk::Label("Copyright (C) 2004 Metaphonic Labs\n", 0.5));
	txtMetaphonic = manage(new Gtk::Label("Metaphonic Labs is...", Gtk::ALIGN_CENTER));

	hcredits = manage(new Gtk::HBox);

	vbleft = manage(new Gtk::VBox);
	vbright = manage(new Gtk::VBox);
	spacer = manage(new Gtk::VBox);
	
	spacer->set_size_request(20, 120);
	vbleft->set_size_request(208, 120);
	vbright->set_size_request(208, 120);
	
	while (*a)
	{
		Gtk::Label *label_author = manage(new Gtk::Label(
			  g_strdup_printf("<b>%s</b>", *a),
			  Gtk::ALIGN_RIGHT));
		Gtk::Label *label_email = manage(new Gtk::Label(*e));

		label_author->set_use_markup(true);
		vbleft->pack_start(*label_author);
		vbright->pack_start(*label_email);

		a++; e++;
	}

	hcredits->pack_start(*vbleft);
	hcredits->pack_start(*spacer);
	hcredits->pack_start(*vbright);
	hcredits->set_size_request(436, 120);

	vbmaster->pack_start(*framebox, true, false);
	vbmaster->pack_start(*txtVersion);
	vbmaster->pack_start(*txtCopyright);
	vbmaster->pack_start(*txtMetaphonic);
	vbmaster->pack_start(*hcredits);

	notebook->append_page(*vbmaster, "Credits");

	show_all_children();
}

AboutBox::~AboutBox (void)
{
}

void AboutBox::onCloseButton (void)
{
	hide ();
}
