/*
 * Copyright (C) 2004-2026 Metaphonic Labs
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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <gtkmm.h>

#include "think.h"

#include "ArgTable.h"
#include "../gthPatchfile.h"

/* Columns to wrap at, at most. Not a width: how many actually appear is
   whatever fits, and this only stops a very wide window laying thirty
   parameters out in one unreadable row. */
static const int MAXCOLS = 3;

ArgTable::ArgTable (void)
    : Gtk::Box(Gtk::Orientation::VERTICAL), chan_(-1)
{
    set_spacing(6);

    nameWidth_ = Gtk::SizeGroup::create(Gtk::SizeGroup::Mode::HORIZONTAL);
    valueWidth_ = Gtk::SizeGroup::create(Gtk::SizeGroup::Mode::HORIZONTAL);
}

ArgTable::~ArgTable (void)
{
    dropArgConns();
}

/* The sliders are about to go; the args they subscribed to are not.
 *
 * Called from the destructor and from the top of reflow(), so that a panel
 * laid out twice does not leave the first set of slots behind pointing at
 * widgets the second set replaced. */
void ArgTable::dropArgConns (void)
{
    for (size_t i = 0; i < argConns_.size(); i++)
        argConns_[i].disconnect();

    argConns_.clear();
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

void ArgTable::insertArg (thArg *arg, const string &group)
{
    if (arg == NULL)
        return;

    pending_.push_back(arg);

    if (!group.empty())
        inferred_[arg] = group;
}

/* Lays out everything handed to insertArg.
 *
 * Deferred to here because the column count depends on how many there are,
 * and that is not known until the last one has arrived.
 *
 * Parameters that declare a group are gathered under a titled expander, in
 * the order the groups were first seen; the ungrouped ones stay in a plain
 * grid above them. An envelope's attack, decay, sustain and release are one
 * control in the way anyone thinks about a patch and four in the way the file
 * stores them, and saying so lets them be folded away when you are working on
 * something else.
 *
 * Expanded to begin with. A parameter you cannot see is a parameter you will
 * not remember the patch has, so folding is something to reach for rather
 * than something to undo on every open. */
void ArgTable::reflow (void)
{
    dropArgConns();

    if (pending_.empty())
        return;

    std::vector<thArg *> loose;
    std::vector<string> order;
    std::map<string, std::vector<thArg *> > groups;

    for (size_t i = 0; i < pending_.size(); i++)
    {
        /* What the patch declared, or failing that what the caller inferred
           from the graph. A declared group is the author saying so and wins;
           an inferred one is a good guess at the same thing. */
        string g = pending_[i]->group();

        if (g.empty() && inferred_.count(pending_[i]))
            g = inferred_[pending_[i]];

        if (g.empty())
        {
            loose.push_back(pending_[i]);
            continue;
        }

        if (!groups.count(g))
            order.push_back(g);

        groups[g].push_back(pending_[i]);
    }

    /* Each group is a block, and the blocks are stacked.
     *
     * They used to be laid side by side, because stacking them undid the
     * columns: a group of five was five rows, and four groups stacked were
     * the tall strip the columns existed to get rid of. That was true while a
     * block was one column wide. Now every block wraps to the width it is
     * given, so a stack of them is a stack of wide rows and the argument has
     * gone -- and side by side does not survive wrapping, because the first
     * block will take all the width it is offered and leave the next one
     * nothing to sit in. */
    std::vector<Gtk::Widget *> blocks;

    /* A group of one is not a group.
     *
     * The inference is literal -- it groups by the node a control drives --
     * and most controls drive a little math::mul of their own, so ts1 came up
     * with eight groups of one beside the single real one. An expander around
     * a lone slider costs a title row and a fold nobody wants and says
     * nothing that the parameter's own label does not.
     *
     * It also hides the worst of the names. A node is called `cutcalc2' or
     * `suscalc' because that is plumbing, not because it is what a person
     * calls that knob; the ones that read well -- `env', `filt' -- are the
     * ones that turn out to have several members anyway. */
    for (size_t i = 0; i < order.size(); i++)
        if (groups[order[i]].size() < 2)
        {
            loose.insert(loose.end(), groups[order[i]].begin(),
                         groups[order[i]].end());
            groups.erase(order[i]);
        }

    {
        std::vector<string> kept;

        for (size_t i = 0; i < order.size(); i++)
            if (groups.count(order[i]))
                kept.push_back(order[i]);

        order.swap(kept);
    }

    if (!loose.empty())
        blocks.push_back(makeFlow(loose, MAXCOLS));

    for (size_t i = 0; i < order.size(); i++)
    {
        Gtk::Expander *exp = manage(new Gtk::Expander(order[i]));

        /* Groups wrap like everything else. A group is a row of a front
           panel -- attack, decay, sustain, release across -- and holding it
           to one column so it read as a list only made sense while it had a
           narrow block to itself. */
        exp->set_expanded(true);
        exp->set_child(*makeFlow(groups[order[i]], MAXCOLS));

        blocks.push_back(exp);
    }

    for (size_t i = 0; i < blocks.size(); i++)
    {
        blocks[i]->set_valign(Gtk::Align::START);

        append(*blocks[i]);
    }

}

/* These parameters, in as many columns as the width will take.
 *
 * The column count used to be worked out from how many there were: one up to
 * eight, two up to twenty, three beyond. It was a guess at the width, made
 * before the panel had been allocated one, and it was wrong in both
 * directions -- three columns held their width whatever the window did, so a
 * narrow window scrolled sideways past sliders squeezed to a nub instead of
 * stacking them.
 *
 * A flow box asks the question at the time it can be answered. Each parameter
 * is one child with a minimum width of its own -- a slider narrower than that
 * is not draggable in any useful way -- so the wrapping falls out of how many
 * of those fit, and the panel's own minimum is one of them. That is what lets
 * the window be dragged down to a single column rather than stopping at
 * whatever three columns needed.
 *
 * Homogeneous, so every column is the same width; combined with the two size
 * groups it means the names, sliders and value boxes line up down the panel
 * rather than each column being its own shape. */
Gtk::FlowBox *ArgTable::makeFlow (const std::vector<thArg *> &args,
                                  int maxPerLine)
{
    Gtk::FlowBox *flow = manage(new Gtk::FlowBox);

    flow->set_selection_mode(Gtk::SelectionMode::NONE);
    flow->set_homogeneous(true);
    flow->set_min_children_per_line(1);
    flow->set_max_children_per_line(maxPerLine);
    flow->set_column_spacing(12);
    flow->set_row_spacing(2);
    flow->set_margin_start(4);
    flow->set_margin_end(4);
    flow->set_margin_top(4);
    flow->set_margin_bottom(4);
    flow->set_valign(Gtk::Align::START);

    for (size_t i = 0; i < args.size(); i++)
        flow->append(*makeRow(args[i]));

    return flow;
}

Gtk::Widget *ArgTable::makeRow (thArg *arg)
{
    Gtk::Box *row = manage(new Gtk::Box(Gtk::Orientation::HORIZONTAL));

    row->set_spacing(8);

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

    /* What HScale(min, max, step) built for us: the value starts at the
       bottom of the range, the page step is ten times the step, and the scale
       rounds to as many digits as the step has. HScale is gone in GTK4 and
       Scale has no such constructor, so the adjustment is spelled out. */
    Gtk::Scale *slider = manage(new Gtk::Scale(
        Gtk::Adjustment::create(lo, lo, hi, .0001, .001, 0),
        Gtk::Orientation::HORIZONTAL));

    slider->set_digits(4);

    /* gtkmm-3: Gtk::Adjustment is refcounted and its constructor is protected,
       so it is handed out as a RefPtr rather than a raw pointer. */
    Glib::RefPtr<Gtk::Adjustment> argAdjust = slider->get_adjustment();

    slider->set_draw_value(false);

    /* The value goes in before anything is listening.
     *
     * The other way round, building the panel moved every slider from its
       adjustment's default to the patch's value, each of which arrived at
       sliderChanged and reported the patch as edited -- so a patch was
       modified the moment it was looked at, and Save lit up for a patch
       nobody had touched. */
    slider->set_value(toDisplay((*arg)[0], units));

    slider->signal_value_changed().connect(
        sigc::bind(
            sigc::mem_fun(*this, &ArgTable::sliderChanged),
            slider, arg->name()));

    /* The other direction: MIDI controllers and the node editor both write
       these args behind the panel's back. The arg is handed to the slot by
       the signal, so only the slider is bound -- and it dies with this
       panel, which is what argConns_ makes sure of. */
    argConns_.push_back(arg->signal_arg_changed().connect(
        sigc::bind(
            sigc::mem_fun(*this, &ArgTable::argChanged),
            slider)));

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
       handle is most of it. Asking for the width is also what tells the flow
       box how much a parameter costs, so it is the number the wrapping is
       decided by, and the panel will drop to one column rather than squeeze
       every slider down to a nub. */
    slider->set_size_request(140, -1);

    /* No ellipsis here.
    
       It was set with only a maximum width, and a label that can ellipsise
       reports the width of "..." as its minimum -- so the table, asked for the
       smallest layout that fits, gave every label exactly that and the panel
       came up as a column of dots. Parameter labels are short ("Pulse Width
       1" is the longest in the corpus at 13 characters), so they can simply
       be allowed their natural width. */
    label->set_xalign(1.0);
    label->set_tooltip_text(arg->name());

    nameWidth_->add_widget(*label);
    valueWidth_->add_widget(*valEntry);

    row->append(*label);
    slider->set_hexpand(true);
    row->append(*slider);
    row->append(*valEntry);

    return row;
}


/* Looked up rather than captured: the arg belongs to the thMidiChan, and
   loading a patch onto this channel replaces the channel. */
void ArgTable::sliderChanged (Gtk::Scale *slider, string name)
{
    if (chan_ < 0)
        return;

    thArg *arg = thSynth::instance()->getChanArg(chan_, name);

    if (arg == NULL)
        return;

    const double want = fromDisplay(slider->get_value(), arg->units());

    /* Nothing to do, and nothing to report, when the slider is only catching
       up with the arg. argChanged moves the slider whenever anything else
       moves the arg, so without this a MIDI controller or a node-editor edit
       came back through here as though someone had dragged the slider, and
       reported the patch as modified. The same guard onAmpSlider carries. */
    if ((double)(*arg)[0] == want)
        return;

    arg->setValue(want);

    /* Moving a slider is editing the patch. Nothing writes it to disk, so
       this is the whole of the record that it happened. */
    gthPatchManager::instance()->markDirty(chan_);
}

void ArgTable::argChanged (thArg *arg, Gtk::Scale *slider)
{
    slider->set_value(toDisplay((*arg)[0], arg->units()));
}
