/* $Id: AboutBox.h,v 1.1 2004/09/16 07:25:39 misha Exp $ */
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

/* FONTS */
#define HELVETICA_20_BFONT "-adobe-helvetica-bold-r-normal-*-20-*-*-*-*-*-*-*"
#define HELVETICA_14_BFONT "-adobe-helvetica-bold-r-normal-*-14-*-*-*-*-*-*-*"
#define HELVETICA_12_BFONT "-adobe-helvetica-bold-r-normal-*-12-*-*-*-*-*-*-*"
#define HELVETICA_12_FONT  "-adobe-helvetica-medium-r-normal-*-12-*-*-*-*-*-*-*"
#define HELVETICA_10_FONT  "-adobe-helvetica-medium-r-normal-*-10-*-*-*-*-*-*-*"
#define CREDITS_FONT       "-misc-fixed-medium-r-normal--13-120-75-75-c-70-" \
"iso8859-1"

#define ABOUT_DEFAULT_WIDTH               100
#define ABOUT_MAX_WIDTH                   600
#define LINE_SKIP                          4

class AboutBox : public Gtk::Window
{
public:
	AboutBox (void);
	~AboutBox (void);

protected:
	Gtk::Fixed          *fixed;
	Gtk::Fixed          *fixed1;
	Gtk::Button         *btnClose;
	Gtk::Notebook       *notebook;
	Gtk::Frame          *frame;
	Gtk::Label          *lblTitle;
	Gtk::Label          *lblCopyright;
	Gtk::ScrolledWindow *scrolledWindow;
	Gtk::TextView       *txtCredits;
	Glib::RefPtr<Gtk::TextBuffer> txtBuf;
	Gtk::Image          *logo;
	Gdk::Pixmap         *pixmap;
	Gdk::Bitmap         *mask;
	Gtk::Adjustment     *adj;
};

#endif /* ABOUT_BOX_H */
