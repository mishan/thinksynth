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

#include <stdio.h>
#include <stdlib.h>

#include <gtkmm.h>

#include "think.h"

#include "ArgTable.h"

ArgTable::ArgTable (void)
    : Gtk::Table(1, 3), rows_(1), args_(0)
{
    set_col_spacings(8);
    set_row_spacings(2);
}

ArgTable::~ArgTable (void)
{
}

/* Samples <-> the unit the value was written in.
 *
 * The engine works in samples and in fractions of TH_MAX, and that is what is
 * stored; these only decide what the panel puts on screen. Both folds the
 * grammar performs are exact and exactly invertible, so a value written
 * `7000 ms' comes back as 7000 and not 6999.97.
 *
 * A unit the author declared rather than wrote as a suffix -- `@x.units =
 * "Hz"' -- passes through untouched. Nothing folded it, so there is nothing
 * to unfold; it is a label. */
double ArgTable::toDisplay (double raw, const string &units)
{
    if (units == "ms")
        return raw * 1000.0 / TH_SAMPLE;

    if (units == "%")
        return raw * 100.0 / TH_MAX;

    return raw;
}

double ArgTable::fromDisplay (double shown, const string &units)
{
    if (units == "ms")
        return shown * TH_SAMPLE / 1000.0;

    if (units == "%")
        return shown * TH_MAX / 100.0;

    return shown;
}

/* Decimal places worth showing for a control whose range runs to `hi'.
 *
 * The resolution that matters is relative: a filter cutoff between 0 and 1
 * needs four places, an envelope time in samples between 0 and 882000 needs
 * none. Anything finer is noise the slider cannot address anyway. */
int ArgTable::decimalsFor (double hi)
{
    const double m = fabs(hi);

    if (m >= 1000) return 0;
    if (m >= 100)  return 1;
    if (m >= 10)   return 2;

    return 4;
}

/* Characters the widest value in this range needs, so nothing is cut off. */
int ArgTable::widthFor (double hi, int digits)
{
    double m = fabs(hi);
    int intDigits = 1;

    while (m >= 10) { m /= 10; intDigits++; }

    /* integer part, the point and its decimals, and one for a minus sign */
    int chars = intDigits + (digits ? digits + 1 : 0) + 1;

    return chars < 6 ? 6 : chars;
}

/* How many parameters go side by side.
 *
 * One column was the whole layout, and ts1 has thirteen parameters while
 * aspect2 has thirty-one -- a strip taller than any window, scrolled past to
 * reach the one you wanted. A slider needs width to be worth dragging, so the
 * columns are few and wide rather than many and thin; three is where a
 * 1200-pixel window still leaves each slider usable.
 *
 * Fixed thresholds rather than a measurement of the allocation: the panel is
 * built before it has been allocated a size, and a layout that reflows while
 * you are reaching for a slider is worse than one that is occasionally a
 * column short. */
int ArgTable::columnsFor (int n)
{
    if (n <= 8)  return 1;
    if (n <= 20) return 2;

    return 3;
}

void ArgTable::insertArg (thArg *arg)
{
    if (arg == NULL)
        return;

    pending_.push_back(arg);
}

/* Lays out everything handed to insertArg.
 *
 * Deferred to here because the column count depends on how many there are, and
 * that is not known until the last one has arrived. */
void ArgTable::reflow (void)
{
    const int n = (int)pending_.size();

    if (n == 0)
        return;

    const int cols = columnsFor(n);

    /* Down each column, then across -- so reading top to bottom gives the
       parameters in order, the way a single column did. Across-then-down would
       scatter alphabetical neighbours over three columns. */
    const int perCol = (n + cols - 1) / cols;

    resize(perCol, cols * 3);
    rows_ = perCol;

    for (int i = 0; i < n; i++)
        placeArg(pending_[i], i / perCol, i % perCol);

    show_all_children();
}

void ArgTable::placeArg (thArg *arg, int col, int row)
{
    const int x = col * 3;

    args_++;

    string text = (arg->label().length() > 0) ? arg->label() : arg->name();

    /* The unit belongs with the name, not beside the number: it is a property
       of the parameter, the same on every row of it, and it costs no width in
       the value column here. */
    if (!arg->units().empty())
        text += " (" + arg->units() + ")";

    Gtk::Label *label = manage(new Gtk::Label(text));
    /* Slider and box both work in display units, so an envelope time runs
       0..20000 ms rather than 0..882000 samples -- the same travel, over a
       number that means something. Only the display converts; what reaches
       thArg::setValue is samples, exactly as before. */
    const string units = arg->units();

    const double lo = toDisplay(arg->min(), units);
    const double hi = toDisplay(arg->max(), units);

    Gtk::HScale *slider = manage(new Gtk::HScale(lo, hi, .0001));

    /* gtkmm-3: Gtk::Adjustment is refcounted and its constructor is protected,
       so it is handed out as a RefPtr rather than a raw pointer. */
    Glib::RefPtr<Gtk::Adjustment> argAdjust = slider->get_adjustment();

    slider->set_draw_value(false);

    slider->signal_value_changed().connect(
        sigc::bind<Gtk::HScale *, thArg *>(
            sigc::mem_fun(*this, &ArgTable::sliderChanged),
            slider, arg));

    arg->signal_arg_changed().connect(
        sigc::bind<Gtk::HScale *>(
            sigc::mem_fun(*this, &ArgTable::argChanged),
            slider));

    slider->set_value(toDisplay((*arg)[0], units));

    /* Decimals to suit the range, and a box wide enough for the result.
     *
     * Four decimals on everything meant `288000.0312' -- eleven characters of
     * which the last four are noise, in a box sized for nine, so it was cut
     * off mid-number. Four places are right for a 0..1 control, where they are
     * the whole resolution; they are meaningless on a range that runs to
     * hundreds of thousands. */
    const int digits = decimalsFor(hi);

    Gtk::SpinButton *valEntry = manage(new Gtk::SpinButton(argAdjust, .0001,
                                                           digits));

    /* The value box was as wide as the slider had left over, which on a
       single column was most of the window for a number four characters
       long. Sized to its content now, so the width goes to the slider. */
    valEntry->set_width_chars(widthFor(hi, digits));

    /* A slider narrower than this is not draggable in any useful way -- the
       handle is most of it. Asking for the width means a window too narrow
       for the columns scrolls, rather than silently squeezing every slider
       down to a nub, which is what happened at 800 pixels. */
    slider->set_size_request(140, -1);

    /* No ellipsis here.
    
       It was set with only a maximum width, and a label that can ellipsise
       reports the width of "..." as its minimum -- so the table, asked for the
       smallest layout that fits, gave every label exactly that and the panel
       came up as a column of dots. Parameter labels are short ("Pulse Width
       1" is the longest in the corpus at 13 characters), so they can simply
       be allowed their natural width. */
    label->set_alignment(Gtk::ALIGN_END, Gtk::ALIGN_CENTER);
    label->set_tooltip_text(arg->name());

    attach(*label, x, x + 1, row, row + 1, Gtk::FILL, Gtk::SHRINK);
    attach(*slider, x + 1, x + 2, row, row + 1,
           Gtk::EXPAND|Gtk::FILL, Gtk::SHRINK|Gtk::FILL);
    attach(*valEntry, x + 2, x + 3, row, row + 1,
           Gtk::SHRINK|Gtk::FILL, Gtk::SHRINK|Gtk::FILL);
}
    
void ArgTable::sliderChanged (Gtk::HScale *slider, thArg *arg)
{
    arg->setValue(fromDisplay(slider->get_value(), arg->units()));
}

void ArgTable::argChanged (thArg *arg, Gtk::HScale *slider)
{
    slider->set_value(toDisplay((*arg)[0], arg->units()));
}
