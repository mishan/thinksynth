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

/* The parameter panel: a grid of label/slider/value rows, in columns.
 *
 * A box rather than a grid, because a patch can put its parameters in named
 * groups -- `@a.group = "Envelope"' -- and each group is drawn as its own
 * titled, foldable block above the loose ones. So this holds a grid for the
 * ungrouped parameters and an expander per group, each with a grid inside. */
class ArgTable : public Gtk::VBox
{
public:
    ArgTable (void);
    ~ArgTable (void);

    /* Records a parameter. Nothing is laid out until reflow(), because the
       column count depends on how many there turn out to be. */
    void insertArg (thArg *arg) { insertArg(arg, ""); }

    /* With a group the caller worked out. A patch may declare `.group'
       itself, and that wins; this is for a group inferred from the graph,
       which is where almost every patch's grouping actually lives. */
    void insertArg (thArg *arg, const string &group);

    /* Lays out everything recorded so far. Call once, after the last
       insertArg. */
    void reflow (void);

private:
    void sliderChanged (Gtk::HScale *, thArg *);
    void argChanged (thArg *, Gtk::HScale *);

    static int columnsFor (int n);

    /* A grid of these parameters, in columns. */
    Gtk::Table *makeGrid (const std::vector<thArg *> &args);
    static double toDisplay (double raw, const string &units);
    static double fromDisplay (double shown, const string &units);
    static int decimalsFor (double hi);
    static int widthFor (double hi, int digits);
    void placeArg (Gtk::Table *grid, thArg *arg, int col, int row);

    std::vector<thArg *> pending_;
    std::map<thArg *, string> inferred_;

    int rows_, args_;
};

#endif /* ARGTABLE_H */
