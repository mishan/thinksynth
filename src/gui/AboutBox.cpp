/* $Id$ */
/*
 * Copyright (C) 2004-2005 Metaphonic Labs
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

	fixed_ = manage(new Gtk::Fixed);
	add(*fixed_);

	btnClose_ = manage(new Gtk::Button("Close"));
	btnClose_->signal_clicked().connect(
		sigc::mem_fun(*this, &AboutBox::onCloseButton));
	fixed_->put(*btnClose_, 384, 383);
	btnClose_->set_size_request(88, 36);
	btnClose_->set_flags(Gtk::CAN_DEFAULT);
	btnClose_->grab_focus();
	btnClose_->grab_default();

	notebook_ = manage(new Gtk::Notebook);
	fixed_->put(*notebook_, 8, 8);
	notebook_->set_size_request(466, 362);

	frame_ = manage(new Gtk::Frame);
	frame_->set_border_width(0);
	frame_->set_size_request(415, 135);
	frame_->set_shadow_type(Gtk::SHADOW_OUT);

	pixmap_ = Gdk::Pixmap::create_from_xpm(get_colormap(), mask_, thinksynth);
	logo_ = manage(new Gtk::Image(pixmap_, mask_));
	frame_->add(*logo_);

	/* Hack to get it to shrink down to our size */
	framebox_ = manage(new Gtk::HBox);
	framebox_->pack_start(*frame_, true, false);
	
	vbmaster_ = manage (new Gtk::VBox);
	
	/* Too bad that Gtk::Labels lose their alignment if the label has >1
	 * line in it. */
#if 0
	header = manage(new Gtk::Label(
		  "Version " PACKAGE_VERSION "\n"
		  "Copyright (C) 2004-2005 Metaphonic Labs\n\n"
		  "Metaphonic Labs is...", Gtk::ALIGN_CENTER));
#endif
	txtVersion_ = manage(new Gtk::Label("Version " PACKAGE_VERSION, 0.5));
	txtCopyright_ = manage(
		new Gtk::Label("Copyright (C) 2004-2005 Metaphonic Labs\n", 0.5));
	txtMetaphonic_ = manage(new Gtk::Label("Metaphonic Labs is...",
										   Gtk::ALIGN_CENTER));

	hcredits_ = manage(new Gtk::HBox);

	vbleft_ = manage(new Gtk::VBox);
	vbright_ = manage(new Gtk::VBox);
	spacer_ = manage(new Gtk::VBox);
	
	spacer_->set_size_request(20, 120);
	vbleft_->set_size_request(208, 120);
	vbright_->set_size_request(208, 120);
	
	while (*a)
	{
		Gtk::Label *label_author_ = manage(new Gtk::Label(
			  g_strdup_printf("<b>%s</b>", *a),
			  Gtk::ALIGN_RIGHT));
		Gtk::Label *label_email_ = manage(new Gtk::Label(*e, Gtk::ALIGN_LEFT));

		label_author_->set_use_markup(true);
		vbleft_->pack_start(*label_author_);
		vbright_->pack_start(*label_email_);

		a++; e++;
	}

	hcredits_->pack_start(*vbleft_);
	hcredits_->pack_start(*spacer_);
	hcredits_->pack_start(*vbright_);
	hcredits_->set_size_request(436, 120);

	vbmaster_->pack_start(*framebox_, true, false);
	vbmaster_->pack_start(*txtVersion_);
	vbmaster_->pack_start(*txtCopyright_);
	vbmaster_->pack_start(*txtMetaphonic_);
	vbmaster_->pack_start(*hcredits_);

	notebook_->append_page(*vbmaster_, "Credits");

	show_all_children();
}

AboutBox::~AboutBox (void)
{
}

void AboutBox::onCloseButton (void)
{
	hide ();
}
