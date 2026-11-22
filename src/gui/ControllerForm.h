/*
 * Copyright (C) 2004-2026 The thinksynth authors
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

#ifndef CONTROLLER_FORM_H
#define CONTROLLER_FORM_H

#include <functional>
#include <set>
#include <string>
#include <vector>

#include <gtkmm.h>

#include "thhForm.h"

/* The configurator's form over the player's profile of a device
   (docs/PHONE.md, "The configurator"), as the page has it
   (wasm/web/profileform.js): a card for each part of the device saying in
   a line what its controls do, opened to change them, and Learn. Every
   edit is hid/thhForm's, written to the player's file, which is then
   read again. */
class ControllerForm : public Gtk::Box
{
public:
    /* `controls' and `own', the device's; `reload(err)' reads the file
       again into what plays, false with why. */
    ControllerForm (const thhControls &controls, const char *own,
                    std::function<bool (std::string &)> reload);

    /* The player's profile, kept at `path'. */
    void setPath (const std::string &path);

    /* The patch's controller block as it plays, and its knobs, for a value
       to ride. Drawn again only if either changed. */
    void setPatch (const std::string &block, const std::vector<std::string> &knobs);

    /* Each control's value now, in the form's order, 0 or 1 for a button;
       none while the device isn't there. */
    void values (const std::vector<double> &v);

    /* Drawn again from the file, `why' an edit was refused said until the
       next, and spoken. */
    void draw (const std::string &why = "");

    /* The text an edit is made to, as the file has it. */
    std::string text () const;

private:
    struct Drawn
    {
        std::string control, card;
        bool        axis;
        Gtk::Label *now;
        Gtk::Box   *box;
    };

    const thhControls                  &controls_;
    const char                         *own_;
    std::function<bool (std::string &)> reload_;
    std::string                         path_, patch_;
    std::string                         shown_;     /* the text drawn */
    std::vector<std::string>            knobs_;

    Gtk::Box          top_;
    Gtk::ToggleButton learn_;
    Gtk::Label        problem_;
    Gtk::Box          cards_;

    std::vector<Drawn>    drawn_;
    std::set<std::string> open_;        /* the cards and Mores opened */
    std::vector<double>   learning_;    /* what Learn started from */
    bool                  learnOn_, backUp_;
    bool                  drawing_;     /* its rows drawn again, the focus
                                           not yet back */

    void say (const std::string &text);

    void edit (const std::string &op, const std::string &control, int which,
               const std::string &fields = "");
    Gtk::Widget *binding (const thhForm &f, const thhFormControl &c,
                          const thHidBinding &b, int which);
    Gtk::Widget *control (const thhForm &f, const thhFormControl &c, Gtk::Label *&now);
    void onLearn ();
};

#endif /* CONTROLLER_FORM_H */
