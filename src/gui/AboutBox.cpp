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

    /* No realize() here. Gtk::Widget::realize is not something to call --
       and in GTK4 the name is ambiguous between the vfunc and the signal
       accessor. It was reaching for a drawable to build the logo against;
       the logo is a Gdk::Texture now and needs no window. */
    fixed_ = manage(new Gtk::Fixed);
    set_child(*fixed_);

    btnClose_ = manage(new Gtk::Button("Close"));
    btnClose_->signal_clicked().connect(
        sigc::mem_fun(*this, &AboutBox::onCloseButton));
    fixed_->put(*btnClose_, 384, 383);
    btnClose_->set_size_request(88, 36);
    btnClose_->grab_focus();

    /* set_can_default and grab_default are gone: a GTK4 window is told which
       widget is its default rather than a widget claiming the role. */
    set_default_widget(*btnClose_);

    notebook_ = manage(new Gtk::Notebook);
    fixed_->put(*notebook_, 8, 8);
    notebook_->set_size_request(466, 362);

    frame_ = manage(new Gtk::Frame);
    frame_->set_margin_start(0);
    frame_->set_margin_end(0);
    frame_->set_margin_top(0);
    frame_->set_margin_bottom(0);
    frame_->set_size_request(415, 135);

    /* create_from_xpm needed a colormap and produced a server-side pixmap plus
       a 1-bit mask. Gdk::Pixbuf reads the same inline XPM data directly and
       keeps the transparency as an alpha channel.
     *
     * A Picture, not an Image. In GTK4 Gtk::Image is for icons and sizes
       whatever it is given to an icon size -- which is why the logo came up
       as a 16-pixel smudge in the middle of a 415x135 frame. Gtk::Picture is
       the widget for a picture, and it draws at the natural size. */
    logoTexture_ = Gdk::Texture::create_for_pixbuf(
        Gdk::Pixbuf::create_from_xpm_data(thinksynth));

    logo_ = manage(new Gtk::Picture(logoTexture_));
    logo_->set_can_shrink(true);
    logo_->set_content_fit(Gtk::ContentFit::CONTAIN);
    frame_->set_child(*logo_);

    /* Hack to get it to shrink down to our size */
    framebox_ = manage(new Gtk::Box(Gtk::Orientation::HORIZONTAL));
    frame_->set_hexpand(true);
    frame_->set_halign(Gtk::Align::CENTER);
    framebox_->append(*frame_);
    
    vbmaster_ = manage (new Gtk::Box(Gtk::Orientation::VERTICAL));
    
    /* Too bad that Gtk::Labels lose their alignment if the label has >1
     * line in it. */
#if 0
    header = manage(new Gtk::Label(
          "Version " PACKAGE_VERSION "\n"
          "Copyright (C) 2004-2014 Metaphonic Labs\n\n"
          "Metaphonic Labs is..."));
#endif
    txtVersion_ = manage(new Gtk::Label("Version " PACKAGE_VERSION, 0.5));
    txtCopyright_ = manage(
        new Gtk::Label("Copyright (C) 2004-2014 Metaphonic Labs\n", 0.5));
    txtMetaphonic_ = manage(new Gtk::Label("Metaphonic Labs is...",
                                           Gtk::Align::CENTER));

    hcredits_ = manage(new Gtk::Box(Gtk::Orientation::HORIZONTAL));

    vbleft_ = manage(new Gtk::Box(Gtk::Orientation::VERTICAL));
    vbright_ = manage(new Gtk::Box(Gtk::Orientation::VERTICAL));
    spacer_ = manage(new Gtk::Box(Gtk::Orientation::VERTICAL));
    
    spacer_->set_size_request(20, 120);
    vbleft_->set_size_request(208, 120);
    vbright_->set_size_request(208, 120);
    
    while (*a)
    {
        Gtk::Label *label_author_ = manage(new Gtk::Label(
              g_strdup_printf("<b>%s</b>", *a),
              Gtk::Align::END));
        Gtk::Label *label_email_ = manage(new Gtk::Label(*e, Gtk::Align::START));

        label_author_->set_use_markup(true);
        label_author_->set_vexpand(true);
        vbleft_->append(*label_author_);
        label_email_->set_vexpand(true);
        vbright_->append(*label_email_);

        a++; e++;
    }

    vbleft_->set_hexpand(true);
    hcredits_->append(*vbleft_);
    spacer_->set_hexpand(true);
    hcredits_->append(*spacer_);
    vbright_->set_hexpand(true);
    hcredits_->append(*vbright_);
    hcredits_->set_size_request(436, 120);

    framebox_->set_vexpand(true);
    framebox_->set_valign(Gtk::Align::CENTER);
    vbmaster_->append(*framebox_);
    txtVersion_->set_vexpand(true);
    vbmaster_->append(*txtVersion_);
    txtCopyright_->set_vexpand(true);
    vbmaster_->append(*txtCopyright_);
    txtMetaphonic_->set_vexpand(true);
    vbmaster_->append(*txtMetaphonic_);
    hcredits_->set_vexpand(true);
    vbmaster_->append(*hcredits_);

    notebook_->append_page(*vbmaster_, "Credits");

}

AboutBox::~AboutBox (void)
{
}

void AboutBox::onCloseButton (void)
{
    hide ();
}
