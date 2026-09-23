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

#include <gtkmm.h>

#include "think.h"

#include "PanelView.h"

/* Columns to wrap at, at most. Not a width: how many actually appear is
   whatever fits, and this only stops a very wide window laying thirty
   parameters out in one unreadable row. */
static const int MAXCOLS = 3;

/* A control narrower than this is not draggable in any useful way -- the
   handle is most of it. Asking for the width is also what tells the flow box
   how much a row costs, so it is the number the wrapping is decided by, and
   the panel drops to one column rather than squeezing every slider to a
   nub. */
static const int SLIDERMIN = 140;

/* An adjustment's travel, out to `value'. An adjustment pins its value to
   lower..upper, so a row that lets a number past its ends has to move the
   ends first or have the number pulled back in. */
static void widen (const Glib::RefPtr<Gtk::Adjustment> &adjust, double value)
{
    if (!isfinite(value))
        return;

    if (value < adjust->get_lower())
        adjust->set_lower(value);

    if (value > adjust->get_upper())
        adjust->set_upper(value);
}

/* The box's own reading of what was typed, where the row is not held to its
   range: the default one parses the text and clamps it to the adjustment,
   which is the one thing a composer's param must not have done to it. So
   the travel is widened to the number before the number is set. */
static void takeTyped (Gtk::SpinButton *box,
                       const Glib::RefPtr<Gtk::Adjustment> &adjust)
{
    box->signal_input().connect([box, adjust] (double &out) -> int
    {
        double v;

        /* Not a number: the default reading, which puts back what the box
           held. */
        if (!thPanelNumberIn(box->get_text(), v))
            return false;

        widen(adjust, v);
        out = v;

        return true;
    }, false);
}

PanelView::PanelView (void)
    : Gtk::Box(Gtk::Orientation::VERTICAL), settingValue_(false)
{
    set_spacing(6);

    nameWidth_ = Gtk::SizeGroup::create(Gtk::SizeGroup::Mode::HORIZONTAL);
    valueWidth_ = Gtk::SizeGroup::create(Gtk::SizeGroup::Mode::HORIZONTAL);
}

PanelView::~PanelView (void)
{
}

/* The widgets go; the size groups stay.
 *
 * A group holds a reference to every widget in it, so one that outlives a
 * rebuild keeps the previous panel's labels alive and sizes the new panel to
 * the widest name of both. Fresh ones are cheaper to say than a removal walk
 * and cannot get it wrong. */
void PanelView::clear (void)
{
    while (Gtk::Widget *child = get_first_child())
        remove(*child);

    bound_.clear();

    nameWidth_ = Gtk::SizeGroup::create(Gtk::SizeGroup::Mode::HORIZONTAL);
    valueWidth_ = Gtk::SizeGroup::create(Gtk::SizeGroup::Mode::HORIZONTAL);
}

/* Each group is a block, and the blocks are stacked.
 *
 * They used to be laid side by side, because stacking them undid the columns:
 * a group of five was five rows, and four groups stacked were the tall strip
 * the columns existed to get rid of. That was true while a block was one
 * column wide. Now every block wraps to the width it is given, so a stack of
 * them is a stack of wide rows and the argument has gone -- and side by side
 * does not survive wrapping, because the first block takes all the width it
 * is offered and leaves the next one nothing to sit in.
 *
 * Expanded to begin with. A parameter you cannot see is a parameter you will
 * not remember the patch has, so folding is something to reach for rather
 * than something to undo on every open. */
void PanelView::setPanel (const thPanel &panel)
{
    clear();

    panel_ = panel;
    bound_.resize(panel_.rows.size());

    /* What the panel is over, where it has anything to say: a panel over
       one node of thirty has to name it, and the ones over a whole channel
       leave both empty and get no header. */
    if (!panel_.title.empty())
    {
        Gtk::Label *title = manage(new Gtk::Label());

        title->set_markup("<b>" + Glib::Markup::escape_text(panel_.title) +
                          "</b>");
        title->set_xalign(0.0);
        title->set_margin_start(4);

        append(*title);
    }

    if (!panel_.subtitle.empty())
    {
        Gtk::Label *subtitle = manage(new Gtk::Label(panel_.subtitle));

        subtitle->set_xalign(0.0);
        subtitle->set_margin_start(4);
        subtitle->set_sensitive(false);

        append(*subtitle);
    }

    std::vector<size_t> loose;
    std::map<string, std::vector<size_t> > grouped;

    for (size_t i = 0; i < panel_.rows.size(); i++)
        if (panel_.rows[i].group.empty())
            loose.push_back(i);
        else
            grouped[panel_.rows[i].group].push_back(i);

    /* groupOrder says what order to draw the groups in. It is not the list
       of which groups there are, and drawing only what it names would build
       a row carrying some other group into bound_ -- findable by indexOf,
       written to by setValue -- without ever appending it to a widget: a
       parameter that is gone from the panel and still reported as being on
       it. thPanelBuilder keeps the two in step; a provider filling a thPanel
       by hand need not, and this is the public boundary. */
    std::vector<string> order = panel_.groupOrder;

    for (size_t i = 0; i < panel_.rows.size(); i++)
    {
        const string &group = panel_.rows[i].group;

        if (group.empty())
            continue;

        size_t at = 0;

        while (at < order.size() && order[at] != group)
            at++;

        if (at == order.size())
            order.push_back(group);
    }

    std::vector<Gtk::Widget *> blocks;

    if (!loose.empty())
        blocks.push_back(makeFlow(loose));

    for (size_t g = 0; g < order.size(); g++)
    {
        const std::vector<size_t> &rows = grouped[order[g]];

        if (rows.empty())
            continue;

        Gtk::Expander *exp = manage(new Gtk::Expander(order[g]));

        /* Groups wrap like everything else. A group is a row of a front
           panel -- attack, decay, sustain, release across -- and holding it
           to one column so it read as a list only made sense while it had a
           narrow block to itself. */
        exp->set_expanded(true);
        exp->set_child(*makeFlow(rows));

        blocks.push_back(exp);
    }

    for (size_t i = 0; i < blocks.size(); i++)
    {
        blocks[i]->set_valign(Gtk::Align::START);

        append(*blocks[i]);
    }

    if (panel_.actions.empty())
        return;

    Gtk::Box *bar = manage(new Gtk::Box(Gtk::Orientation::HORIZONTAL));

    bar->set_spacing(6);
    bar->set_halign(Gtk::Align::END);

    for (size_t i = 0; i < panel_.actions.size(); i++)
    {
        Gtk::Button *b = manage(new Gtk::Button(panel_.actions[i].label));

        b->set_sensitive(panel_.actions[i].enabled);
        b->signal_clicked().connect(
            sigc::bind(sigc::mem_fun(*this, &PanelView::onAction),
                       panel_.actions[i].id));

        bar->append(*b);
    }

    append(*bar);
}

/* These rows, in as many columns as the width will take.
 *
 * The column count used to be worked out from how many there were: one up to
 * eight, two up to twenty, three beyond. It was a guess at the width, made
 * before the panel had been allocated one, and it was wrong in both
 * directions -- three columns held their width whatever the window did, so a
 * narrow window scrolled sideways past sliders squeezed to a nub instead of
 * stacking them.
 *
 * A flow box asks the question at the time it can be answered. Each row is
 * one child with a minimum width of its own, so the wrapping falls out of how
 * many of those fit, and the panel's own minimum is one of them. That is what
 * lets the window be dragged down to a single column rather than stopping at
 * whatever three columns needed.
 *
 * Homogeneous, so every column is the same width; combined with the two size
 * groups it means the names, controls and value boxes line up down the panel
 * rather than each column being its own shape. */
Gtk::FlowBox *PanelView::makeFlow (const std::vector<size_t> &rows)
{
    Gtk::FlowBox *flow = manage(new Gtk::FlowBox);

    flow->set_selection_mode(Gtk::SelectionMode::NONE);
    flow->set_homogeneous(true);
    flow->set_min_children_per_line(1);
    flow->set_max_children_per_line(MAXCOLS);
    flow->set_column_spacing(12);
    flow->set_row_spacing(2);
    flow->set_margin_start(4);
    flow->set_margin_end(4);
    flow->set_margin_top(4);
    flow->set_margin_bottom(4);
    flow->set_valign(Gtk::Align::START);

    for (size_t i = 0; i < rows.size(); i++)
        flow->append(*makeRow(rows[i]));

    return flow;
}

Gtk::Widget *PanelView::makeRow (size_t at)
{
    const thPanelRow &row = panel_.rows[at];

    bound_[at].row = row;

    string text = row.label;

    /* The unit belongs with the name, not beside the number: it is a
       property of the parameter, the same on every row of it, and it costs
       no width in the value column here. */
    if (!row.units.empty())
        text += " (" + row.units + ")";

    Gtk::Label *label = manage(new Gtk::Label(text));

    /* No ellipsis here.
     *
     * It was set with only a maximum width, and a label that can ellipsise
     * reports the width of "..." as its minimum -- so the table, asked for
     * the smallest layout that fits, gave every label exactly that and the
     * panel came up as a column of dots. Parameter labels are short ("Pulse
     * Width 1" is the longest in the corpus at 13 characters), so they can
     * simply be allowed their natural width. */
    label->set_xalign(1.0);

    /* What the author said it is for, and failing that what the file calls
       it -- which is worth having when the label is something else. */
    label->set_tooltip_text(row.desc.empty() ? row.id : row.desc);

    nameWidth_->add_widget(*label);

    Gtk::Box *box = manage(new Gtk::Box(Gtk::Orientation::HORIZONTAL));

    box->set_spacing(8);
    box->append(*label);
    box->append(*makeControl(at));

    /* And the menus that are about the value rather than being it. They go
       after the control because they qualify what is already there -- "4000,
       milliseconds, read from no knob" is the order it is said in. */
    if (!row.unitChoices.empty())
        box->append(*makeUnit(at));

    if (row.bindable)
        box->append(*makeBind(at));

    return box;
}

/* Which of the units a number is written in.
 *
 * A menu and not a label, because on a composer's duration the unit is part
 * of what the author said: `period = 4 beats' follows the tempo and
 * `period = 2 s' does not, and the two are different pieces rather than two
 * spellings of one. Changing it keeps the number and changes what it means,
 * which is what somebody reaching for this menu is saying. */
Gtk::Widget *PanelView::makeUnit (size_t at)
{
    const thPanelRow &row = panel_.rows[at];

    std::vector<Glib::ustring> shown;
    size_t sel = 0;

    for (size_t i = 0; i < row.unitChoices.size(); i++)
    {
        shown.push_back(row.unitChoices[i]);

        if (row.unitChoices[i] == row.units)
            sel = i;
    }

    Gtk::DropDown *unit = manage(new Gtk::DropDown(shown));

    bound_[at].unit = unit;

    /* Before anything is listening, for the reason the slider gives. */
    unit->set_selected((guint)sel);

    /* Offered even while the number is not. A bound duration carries no
       unit in the file and reads as seconds; picking one here is how the
       unbinding that follows knows what to write. */
    unit->property_selected().signal_changed().connect(
        sigc::bind(sigc::mem_fun(*this, &PanelView::onUnit), at));

    return unit;
}

/* The knob this value is read through, or none of them.
 *
 * `(value)' first, so that letting a binding go is one press rather than a
 * thing to work out. A bound row's number is shown and not offered -- what
 * moves it is the knob -- which makes this the only control on such a row
 * that does anything, and the reason it is drawn whether or not the number
 * beside it is sensitive. */
Gtk::Widget *PanelView::makeBind (size_t at)
{
    const thPanelRow &row = panel_.rows[at];

    std::vector<Glib::ustring> shown;
    size_t sel = 0;

    shown.push_back("(value)");

    for (size_t i = 0; i < panel_.knobs.size(); i++)
    {
        shown.push_back("@" + panel_.knobs[i]);

        if (panel_.knobs[i] == row.knob)
            sel = i + 1;
    }

    Gtk::DropDown *bind = manage(new Gtk::DropDown(shown));

    bound_[at].bind = bind;

    bind->set_selected((guint)sel);

    bind->property_selected().signal_changed().connect(
        sigc::bind(sigc::mem_fun(*this, &PanelView::onBind), at));

    return bind;
}

Gtk::Widget *PanelView::makeControl (size_t at)
{
    switch (panel_.rows[at].kind)
    {
        case thPanelRow::CHOICE:   return makeChoice(at);
        case thPanelRow::NUMBER:   return makeNumber(at);
        case thPanelRow::TEXT:     return makeText(at);
        case thPanelRow::READONLY: return makeRead(at);
        case thPanelRow::TOGGLE:   return makeToggle(at);
        case thPanelRow::SLIDER:
        default:                   return makeSlider(at);
    }
}

/* A range to drag and a box to type in, sharing one adjustment.
 *
 * Both work in display units, so an envelope time runs 0..20000 ms rather
 * than 0..882000 samples. Only the display converts; what reaches the arg is
 * whatever the provider folds the spelling back to. */
Gtk::Widget *PanelView::makeSlider (size_t at)
{
    const thPanelRow &row = panel_.rows[at];
    Bound &bound = bound_[at];

    Gtk::Box *box = manage(new Gtk::Box(Gtk::Orientation::HORIZONTAL));

    box->set_spacing(8);
    box->set_hexpand(true);

    /* What HScale(min, max, step) built for us: the page step is ten times
       the step, and the scale rounds to as many digits as the step has.
       HScale is gone in GTK4 and Scale has no such constructor, so the
       adjustment is spelled out.
     *
     * The rounding is the row's resolution and not four places whatever the
     * range. Four places on a range that runs to thousands meant the slider
     * held 1234.5678 while the box beside it read 1235 -- a display that
     * disagreed with the thing it was attached to, and an edit that carried
     * a number nobody had seen. */
    bound.adjust = Gtk::Adjustment::create(row.lo, row.lo, row.hi,
                                           row.step, row.step * 10, 0);

    Gtk::Scale *slider = manage(new Gtk::Scale(
        bound.adjust, Gtk::Orientation::HORIZONTAL));

    /* round_digits as well as digits, explicitly. set_digits() does round
       the adjustment as a side effect, but only while round-digits has not
       been set to anything, which is a subtlety to rely on rather than a
       rule. */
    slider->set_digits(row.decimals);
    slider->set_round_digits(row.decimals);
    slider->set_draw_value(false);
    slider->set_size_request(SLIDERMIN, -1);
    slider->set_hexpand(true);

    Gtk::SpinButton *valEntry = manage(new Gtk::SpinButton(
        bound.adjust, row.step, row.decimals));

    /* The value box was as wide as the slider had left over, which on a
       single column was most of the window for a number four characters
       long. Sized to its content now, so the width goes to the slider. */
    valEntry->set_width_chars(row.valueChars);

    if (!row.bounded)
        takeTyped(valEntry, bound.adjust);

    valueWidth_->add_widget(*valEntry);

    slider->set_sensitive(row.editable);
    valEntry->set_sensitive(row.editable);

    /* The value goes in before anything is listening.
     *
     * The other way round, building the panel moved every slider from its
     * adjustment's default to the patch's value, each of which arrived at
     * the handler as though someone had dragged it -- so a patch was
     * modified the moment it was looked at, and Save lit up for a patch
     * nobody had touched. */
    bound.adjust->set_value(row.value);

    bound.adjust->signal_value_changed().connect(
        sigc::bind(sigc::mem_fun(*this, &PanelView::onAdjust), at));

    box->append(*slider);
    box->append(*valEntry);

    return box;
}

/* A number with no useful range to drag through: the box without the
   slider. */
Gtk::Widget *PanelView::makeNumber (size_t at)
{
    const thPanelRow &row = panel_.rows[at];
    Bound &bound = bound_[at];

    bound.adjust = Gtk::Adjustment::create(row.lo, row.lo, row.hi,
                                           row.step, row.step * 10, 0);

    Gtk::SpinButton *valEntry = manage(new Gtk::SpinButton(
        bound.adjust, row.step, row.decimals));

    valEntry->set_width_chars(row.valueChars);
    valEntry->set_sensitive(row.editable);
    valEntry->set_hexpand(true);

    if (!row.bounded)
        takeTyped(valEntry, bound.adjust);

    valueWidth_->add_widget(*valEntry);

    bound.adjust->set_value(row.value);

    bound.adjust->signal_value_changed().connect(
        sigc::bind(sigc::mem_fun(*this, &PanelView::onAdjust), at));

    return valEntry;
}

/* Named values are a list, not a scale. Six waveforms have no order worth
   dragging through and five sixths of the travel between them means
   nothing, so a slider is the wrong shape for the question. */
Gtk::Widget *PanelView::makeChoice (size_t at)
{
    const thPanelRow &row = panel_.rows[at];
    Bound &bound = bound_[at];

    std::vector<Glib::ustring> shown;

    for (size_t i = 0; i < row.choices.size(); i++)
        shown.push_back(row.choices[i].first);

    Gtk::DropDown *choice = manage(new Gtk::DropDown(shown));

    bound.choice = choice;

    /* Before anything is listening, for the reason the slider gives. */
    const int sel = thPanelChoiceIndex(row.choices, row.value);

    choice->set_selected(sel < 0 ? GTK_INVALID_LIST_POSITION : (guint)sel);

    choice->set_sensitive(row.editable);
    choice->set_hexpand(true);

    choice->property_selected().signal_changed().connect(
        sigc::bind(sigc::mem_fun(*this, &PanelView::onChoice), at));

    return choice;
}

Gtk::Widget *PanelView::makeText (size_t at)
{
    const thPanelRow &row = panel_.rows[at];
    Bound &bound = bound_[at];

    Gtk::Entry *entry = manage(new Gtk::Entry);

    bound.entry = entry;

    entry->set_text(row.text);
    entry->set_sensitive(row.editable);
    entry->set_hexpand(true);

    /* On activate rather than on every keystroke: half of a note set is a
       note set the provider would refuse, and a refusal per character is not
       a thing to put in front of anyone. */
    entry->signal_activate().connect(
        sigc::bind(sigc::mem_fun(*this, &PanelView::onEntry), at));

    return entry;
}

/* Shown, not offered. Hiding an output outright would be worse: seeing what
   a patch produces is half of reading one. */
Gtk::Widget *PanelView::makeRead (size_t at)
{
    Gtk::Label *readout = manage(new Gtk::Label(panel_.rows[at].text));

    bound_[at].readout = readout;

    readout->set_xalign(0.0);
    readout->set_hexpand(true);
    readout->set_sensitive(false);

    return readout;
}

Gtk::Widget *PanelView::makeToggle (size_t at)
{
    const thPanelRow &row = panel_.rows[at];

    Gtk::CheckButton *toggle = manage(new Gtk::CheckButton);

    bound_[at].toggle = toggle;

    toggle->set_active(row.value != 0);
    toggle->set_sensitive(row.editable);
    toggle->set_hexpand(true);

    toggle->signal_toggled().connect(
        sigc::bind(sigc::mem_fun(*this, &PanelView::onToggle), at));

    return toggle;
}

void PanelView::setValue (const string &row, double display)
{
    const int at = panel_.indexOf(row);

    if (at < 0)
        return;

    Bound &b = bound_[(size_t)at];

    b.row.value = display;

    settingValue_ = true;

    if (b.adjust)
    {
        if (!b.row.bounded)
            widen(b.adjust, display);

        b.adjust->set_value(display);
    }
    else if (b.choice)
    {
        const int sel = thPanelChoiceIndex(b.row.choices, display);

        b.choice->set_selected(sel < 0 ? GTK_INVALID_LIST_POSITION
                                       : (guint)sel);
    }
    else if (b.toggle)
        b.toggle->set_active(display != 0);
    else if (b.entry)
        b.entry->set_text(thPanelSpell(display, b.row.decimals));
    else if (b.readout)
        b.readout->set_text(thPanelSpell(display, b.row.decimals));

    settingValue_ = false;
}

void PanelView::setText (const string &row, const string &text)
{
    const int at = panel_.indexOf(row);

    if (at < 0)
        return;

    Bound &b = bound_[(size_t)at];

    b.row.text = text;

    settingValue_ = true;

    if (b.entry)
        b.entry->set_text(text);
    else if (b.readout)
        b.readout->set_text(text);

    settingValue_ = false;
}

void PanelView::emitEdit (const string &row, const string &valueText)
{
    if (settingValue_)
        return;

    signal_edited_.emit(row, valueText);
}

/* What the box shows is what the intent carries.
 *
 * The adjustment rounds to the row's resolution, so the spelling is exact
 * rather than a rounding of something finer -- and the provider parses the
 * same characters a person would have typed into the box. One spelling, one
 * value, whichever of the two the control was moved by. */
void PanelView::onAdjust (size_t at)
{
    Bound &b = bound_[at];

    const double now = b.adjust->get_value();
    const string spelled = thPanelSpell(now, b.row.decimals);

    /* The box rounding what it was given, and not an edit. A value finer
       than the row shows -- 0.3333 in a box of two places -- is read back as
       0.33 when the box loses the focus, and writing that would change the
       file for somebody who only tabbed past it. */
    if (spelled == thPanelSpell(b.row.value, b.row.decimals))
        return;

    b.row.value = now;

    emitEdit(b.row.id, spelled);
}

void PanelView::onChoice (size_t at)
{
    const Bound &b = bound_[at];

    const guint sel = b.choice->get_selected();

    /* No row selected is what a value the plugin does not implement looks
       like. It is a state the list can be in and not an edit anyone made. */
    if (sel == GTK_INVALID_LIST_POSITION || sel >= b.row.choices.size())
        return;

    emitEdit(b.row.id, b.row.choices[sel].first);
}

void PanelView::onEntry (size_t at)
{
    const Bound &b = bound_[at];

    emitEdit(b.row.id, b.entry->get_text());
}

void PanelView::onToggle (size_t at)
{
    const Bound &b = bound_[at];

    emitEdit(b.row.id, b.toggle->get_active() ? "1" : "0");
}

/* The unit alone, which is the whole of what this control says. The number
   it applies to is the one the provider already has. */
void PanelView::onUnit (size_t at)
{
    const Bound &b = bound_[at];

    const guint sel = b.unit->get_selected();

    if (sel >= b.row.unitChoices.size())
        return;

    emitEdit(b.row.id, b.row.unitChoices[sel]);
}

/* `@name' to bind, `@' on its own to let go -- the spelling StagePanel.h
   documents, and the reason a bare `@' is safe to mean it is that no knob
   has an empty name. */
void PanelView::onBind (size_t at)
{
    const Bound &b = bound_[at];

    const guint sel = b.bind->get_selected();

    if (sel == GTK_INVALID_LIST_POSITION || sel > panel_.knobs.size())
        return;

    emitEdit(b.row.id, sel == 0 ? "@" : "@" + panel_.knobs[sel - 1]);
}

void PanelView::onAction (string id)
{
    signal_action_.emit(id);
}
