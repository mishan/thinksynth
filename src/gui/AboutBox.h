/* $Id: AboutBox.h,v 1.6 2004/09/17 04:56:16 joshk Exp $ */
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

#ifndef ABOUT_BOX_H
#define ABOUT_BOX_H

class AboutBox : public Gtk::Window
{
public:
	AboutBox (void);
	~AboutBox (void);

protected:
	void onCloseButton (void);

	Gtk::Fixed          *fixed;
	Gtk::Button         *btnClose;
	Gtk::Notebook       *notebook;
	Gtk::Frame          *frame;
	Gtk::Image          *logo;
	Gtk::VBox           *vbmaster;
	Gtk::VBox           *vbleft;
	Gtk::VBox           *vbright;
	Gtk::VBox           *spacer;
	Gtk::HBox           *hcredits;
	Gtk::HBox           *framebox;
	Gtk::Label          *txtVersion;
	Gtk::Label          *txtCopyright;
	Gtk::Label          *txtMetaphonic;
	
	Glib::RefPtr<Gtk::TextBuffer>     txtBuf;
	Glib::RefPtr<Gdk::Pixmap>         pixmap;
	Glib::RefPtr<Gdk::Bitmap>         mask;
};

#endif /* ABOUT_BOX_H */
