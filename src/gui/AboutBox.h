/* $Id: AboutBox.h,v 1.3 2004/09/16 09:36:40 misha Exp $ */
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

#define CREDITS_TEXT "\
       Main Programming: Leif M. Ames \n\
                         <ink@bespin.org>\n\
                         Misha Nasledov \n\
                         <misha@nasledov.com>\n\
                         Joshua Kwan\n\
                         <joshk@triplehelix.org>\n\
                         Aaron Lehmann\n\
                         <aaronl@vitelus.com>\n\n\
"


/* FONTS */
#define CREDITS_FONT       "monospace 9"

class AboutBox : public Gtk::Window
{
public:
	AboutBox (void);
	~AboutBox (void);

protected:
	void onCloseButton (void);

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
