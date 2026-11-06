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

#ifndef CONTROLLER_PANE_H
#define CONTROLLER_PANE_H

#include <string>
#include <vector>

#include <gtkmm.h>

#include "thhPane.h"

/* A controller's profile, read only: a row per binding with what it does,
   where, and what the control is doing now, and what won't work. Reload
   reads the file again; Open file opens it in the desktop's editor,
   writing the controller's own profile there first if there is none. */
class ControllerPane : public Gtk::Box
{
public:
    ControllerPane (void);

    /* NULL where there is nothing to read a controller with. */
    void setSource (thhPaneSource *source);

private:
    thhPaneSource *source_;
    unsigned       generation_;
    std::string    problem_;    /* the last Reload's or Open's, until the next */

    Gtk::Label  status_;
    Gtk::Grid   grid_;
    Gtk::Box    buttons_;
    Gtk::Button reload_, open_;
    /* A binding's labels: the control and its value now, what it does and
       where, and what won't work. */
    struct Cells
    {
        Gtk::Label *head, *now, *does, *warning;
    };
    std::vector<Cells> cells_;
    sigc::connection tick_;

    bool onTick (void);
    void onReload (void);
    void onOpen (void);
    void rebuild (size_t rows);
};

#endif /* CONTROLLER_PANE_H */
