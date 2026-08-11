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

    /* Records a parameter. Nothing is laid out until reflow(), because the
       column count depends on how many there turn out to be. */
    void insertArg (thArg *arg);

    /* Lays out everything recorded so far. Call once, after the last
       insertArg. */
    void reflow (void);

private:
    void sliderChanged (Gtk::HScale *, thArg *);
    void argChanged (thArg *, Gtk::HScale *);

    static int columnsFor (int n);
    void placeArg (thArg *arg, int col, int row);

    std::vector<thArg *> pending_;

    int rows_, args_;
};

#endif /* ARGTABLE_H */
