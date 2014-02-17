/*
 * Copyright (C) 2004-2014 Metaphonic Labs
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

#ifndef ABOUT_BOX_H
#define ABOUT_BOX_H

class AboutBox : public Gtk::Window
{
public:
	AboutBox (void);
	~AboutBox (void);

protected:
	void onCloseButton (void);

	Gtk::Fixed          *fixed_;
	Gtk::Button         *btnClose_;
	Gtk::Notebook       *notebook_;
	Gtk::Frame          *frame_;
	Gtk::Image          *logo_;
	Gtk::VBox           *vbmaster_;
	Gtk::VBox           *vbleft_;
	Gtk::VBox           *vbright_;
	Gtk::VBox           *spacer_;
	Gtk::HBox           *hcredits_;
	Gtk::HBox           *framebox_;
	Gtk::Label          *txtVersion_;
	Gtk::Label          *txtCopyright_;
	Gtk::Label          *txtMetaphonic_;
	
	Glib::RefPtr<Gtk::TextBuffer>     txtBuf_;
	Glib::RefPtr<Gdk::Pixmap>         pixmap_;
	Glib::RefPtr<Gdk::Bitmap>         mask_;
};

#endif /* ABOUT_BOX_H */
