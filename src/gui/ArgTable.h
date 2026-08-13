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

/* The parameter panel: label/slider/value rows, in as many columns as fit.
 *
 * A box rather than a grid, because a patch can put its parameters in named
 * groups -- `@a.group = "Envelope"' -- and each group is drawn as its own
 * titled, foldable block above the loose ones. So this holds a flow of the
 * ungrouped parameters and an expander per group, each with a flow inside. */
class ArgTable : public Gtk::Box
{
public:
    ArgTable (void);
    ~ArgTable (void);

    /* Which channel these parameters belong to, so that moving one can say
       the patch has been edited. -1 for none. */
    void setChannel (int chan) { chan_ = chan; }

    /* Records a parameter. Nothing is laid out until reflow(). */
    void insertArg (thArg *arg) { insertArg(arg, ""); }

    /* With a group the caller worked out. A patch may declare `.group'
       itself, and that wins; this is for a group inferred from the graph,
       which is where almost every patch's grouping actually lives. */
    void insertArg (thArg *arg, const string &group);

    /* Lays out everything recorded so far. Call once, after the last
       insertArg. The column count is not decided here -- see makeFlow. */
    void reflow (void);

private:
    /* Both take the parameter's *name*, not a thArg *.
     *
     * Loading a patch onto this channel replaces the channel and frees every
     * arg on it, and the panel is rebuilt from the same signal -- so a bound
     * thArg * is a pointer this panel does not own, cannot be told about, and
     * has no way to check. The patch bar's amplitude slider has always looked
     * its arg up through the channel number for this reason; this is the same
     * move applied to the rest of them. */
    void sliderChanged (Gtk::Scale *, string name);
    void argChanged (thArg *, Gtk::Scale *);

    /* Drops every subscription in argConns_. */
    void dropArgConns (void);

    /* One parameter: its name, a slider and a value box, side by side. */
    Gtk::Widget *makeRow (thArg *arg);

    /* These parameters, wrapped into however many columns the width allows. */
    Gtk::FlowBox *makeFlow (const std::vector<thArg *> &args, int maxPerLine);

    static double toDisplay (double raw, const string &units);
    static double fromDisplay (double shown, const string &units);
    static int decimalsFor (double hi);
    static int widthFor (double hi, int digits);

    int chan_;

    std::vector<thArg *> pending_;
    std::map<thArg *, string> inferred_;

    /* The args outlive the sliders subscribed to them. Glib::ObjectBase is a
       sigc::trackable so the slot would be dropped when this panel dies
       anyway, but that is a property of the base class rather than something
       this file says, and it does not cover a reflow() that runs twice.
       Held and dropped explicitly instead. */
    std::vector<sigc::connection> argConns_;

    /* Every name shares a width and every value box shares a width, across
       the whole panel and not merely within one block. Each row is its own
       little box now, so without this there are no columns to line up in --
       and lining them up across the groups as well is better than the grids
       managed, where each block worked its widths out alone. */
    Glib::RefPtr<Gtk::SizeGroup> nameWidth_;
    Glib::RefPtr<Gtk::SizeGroup> valueWidth_;
};

#endif /* ARGTABLE_H */
