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

#ifndef ARGTABLE_H
#define ARGTABLE_H

class ArgTable : public Gtk::Table
{
public:
    ArgTable (void);
    ~ArgTable (void);

    void insertArg (thArg *arg);
private:
    void sliderChanged (Gtk::HScale *, thArg *);
    void argChanged (thArg *, Gtk::HScale *);

    int rows_, args_;
};

#endif /* ARGTABLE_H */
