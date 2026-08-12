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
    Gtk::Picture        *logo_;
    Gtk::Box            *vbmaster_;
    Gtk::Box            *vbleft_;
    Gtk::Box            *vbright_;
    Gtk::Box            *spacer_;
    Gtk::Box            *hcredits_;
    Gtk::Box            *framebox_;
    Gtk::Label          *txtVersion_;
    Gtk::Label          *txtCopyright_;
    Gtk::Label          *txtMetaphonic_;
    
    Glib::RefPtr<Gtk::TextBuffer>     txtBuf_;

    /* gtkmm-3 removed Gdk::Pixmap and Gdk::Bitmap along with the whole
       server-side drawable API; a Pixbuf carries its own alpha, so the
       separate mask is gone too. GTK4 wants a paintable rather than a pixbuf,
       and the pixbuf is only the thing the XPM is read into on the way. */
    Glib::RefPtr<Gdk::Texture>        logoTexture_;
};

#endif /* ABOUT_BOX_H */
