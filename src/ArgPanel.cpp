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

#include <stdlib.h>

#include <cmath>

#include "think.h"

#include "ArgPanel.h"

ArgPanel::ArgPanel (void)
    : chan_(-1)
{
}

void ArgPanel::exclude (const string &name)
{
    for (size_t i = 0; i < hidden_.size(); i++)
        if (hidden_[i] == name)
            return;

    hidden_.push_back(name);
}

thArg *ArgPanel::argFor (const string &row) const
{
    thSynth *synth = thSynth::instance();

    if (synth == NULL || chan_ < 0)
        return NULL;

    return synth->getChanArg(chan_, prefix_ + row);
}

thSynthTree *ArgPanel::tree (void) const
{
    thSynth *synth = thSynth::instance();

    if (synth == NULL || chan_ < 0)
        return NULL;

    if (!prefix_.empty())
    {
        thChanEffect *fx = synth->getEffect(chan_);

        return fx ? fx->tree() : NULL;
    }

    thMidiChan *chan = synth->getChannel(chan_);

    return chan ? chan->modnode() : NULL;
}

std::map<string, string> ArgPanel::inferGroups (thSynthTree *tree)
{
    std::map<string, string> host;
    std::map<string, int> uses;

    if (tree == NULL)
        return host;

    const thSynthTree::NodeMap &nodes = tree->nodes();

    for (thSynthTree::NodeMap::const_iterator n = nodes.begin();
         n != nodes.end(); ++n)
    {
        if (n->second == NULL)
            continue;

        const thArgMap &args = n->second->args();

        for (thArgMap::const_iterator a = args.begin(); a != args.end(); ++a)
        {
            if (a->second == NULL ||
                a->second->type() != thArg::ARG_CHANNEL)
                continue;

            const string ctl = a->second->argPtrName();

            uses[ctl]++;
            host[ctl] = n->second->name();
        }
    }

    for (std::map<string, int>::iterator u = uses.begin();
         u != uses.end(); ++u)
        if (u->second != 1)
            host.erase(u->first);

    return host;
}

/* One parameter, described.
 *
 * Named values are a list and not a range: six waveforms have no order worth
 * dragging through, and five sixths of the travel between them means nothing.
 * Everything else is a slider, in display units, so an envelope time runs
 * 0..20000 ms rather than 0..882000 samples -- the same travel over a number
 * that means something. */
static thPanelRow rowFor (thArg *arg)
{
    thPanelRow row;

    row.id = arg->name();
    row.label = arg->label().empty() ? arg->name() : arg->label();
    row.desc = arg->comment();
    row.units = arg->units();

    const vector<string> &names = arg->valueNames();

    for (size_t i = 0; i < names.size(); i++)
        if (!names[i].empty())
            row.choices.push_back(make_pair(names[i], (int)i));

    if (!row.choices.empty())
    {
        /* A selector is a number in the file and a name on the panel. No
           unit conversion either way: `Square' is not 2 milliseconds of
           anything. */
        row.kind = thPanelRow::CHOICE;
        row.value = (*arg)[0];

        /* Its travel is the list, whatever the file put in `.min' and
           `.max' -- a list of names implies a step of one and a range
           running from its first named entry to its last, which is what
           thArg::valueNames says about itself. */
        row.lo = row.choices.front().second;
        row.hi = row.choices.back().second;
        row.step = 1;
        row.decimals = 0;
        row.valueChars = thPanelValueChars(row.hi, 0);

        const int at = thPanelChoiceIndex(row.choices, row.value);

        row.text = (at < 0) ? string() : row.choices[at].first;

        return row;
    }

    /* A parameter its plugin reads as a whole number shows no decimals and
       cannot be left between two of them. `waveform' is read
       `switch ((int)x)', so 3.4 is a triangle spelled misleadingly, and a
       control that can produce it is a control most of whose travel does
       nothing. */
    const bool whole = (arg->step() == 1);

    row.kind = thPanelRow::SLIDER;
    row.lo = thPanelToDisplay(arg->min(), row.units);
    row.hi = thPanelToDisplay(arg->max(), row.units);
    row.decimals = whole ? 0 : thPanelDecimals(row.hi);
    row.step = pow(10.0, -row.decimals);
    row.valueChars = thPanelValueChars(row.hi, row.decimals);
    row.value = thPanelToDisplay((*arg)[0], row.units);
    row.text = thPanelSpell(row.value, row.decimals);

    return row;
}

bool ArgPanel::offers (const string &row, thArg *arg) const
{
    /* HIDE is the file saying not to draw it; CHANARG is not a control of
       this channel's. Only a SLIDER is a parameter a person sets. */
    if (arg == NULL || arg->widgetType() != thArg::SLIDER)
        return false;

    for (size_t i = 0; i < hidden_.size(); i++)
        if (hidden_[i] == row)
            return false;

    return true;
}

bool ArgPanel::build (thPanel &out) const
{
    out = thPanel();
    out.kind = thPanel::CHANARG;
    out.a = chan_;

    thSynth *synth = thSynth::instance();

    if (synth == NULL || chan_ < 0)
        return false;

    /* A channel's two arg maps are two maps and not one merged: an
       instrument's `@a' and an effect's must not collide. Which of them this
       panel is over is the prefix, and so is which one a lookup reaches. */
    const thArgMap args = prefix_.empty() ? synth->getChanArgs(chan_)
                                          : synth->getEffectArgs(chan_);

    const std::map<string, string> groups = inferGroups(tree());

    thPanelBuilder build;

    for (thArgMap::const_iterator a = args.begin(); a != args.end(); ++a)
    {
        thArg *arg = a->second;

        if (!offers(a->first, arg))
            continue;

        const std::map<string, string>::const_iterator g =
            groups.find(a->first);

        build.add(rowFor(arg), arg->group(),
                  (g == groups.end()) ? string() : g->second);
    }

    build.finish(out);

    return !out.rows.empty();
}

bool ArgPanel::valueFor (const string &row, double &display) const
{
    thArg *arg = argFor(row);

    if (arg == NULL)
        return false;

    display = rowFor(arg).value;

    return true;
}

thPanelResult ArgPanel::propose (const string &row, const string &valueText,
                                 thPanelEdit &out) const
{
    out = thPanelEdit();
    out.kind = thPanelEdit::CHANARG;
    out.row = row;
    out.valueText = valueText;
    out.a = chan_;

    thArg *arg = argFor(row);

    /* Not an error a shell can do anything about, but not silence either: a
       row whose arg has gone is a panel drawn over a channel that has since
       been loaded again, and saying so is how that gets noticed. The same
       answer for an arg that is there and is not a row of this panel -- an
       output, or the amplitude the patch bar has already -- since from the
       panel's side those are the same thing: no such row. */
    if (!offers(row, arg))
        return thPanelResult::refuse("no parameter called " + row);

    const thPanelRow described = rowFor(arg);

    double want = 0;

    if (described.kind == thPanelRow::CHOICE)
    {
        /* By name first: that is what the panel shows and what a person
           picked. The number is the fallback, because an intent that has
           crossed a wire carries what the file would say -- and it is tried
           only when the text *is* a number, since atof() reads a misspelled
           name as 0 and would then quietly select whatever value 0 means. */
        size_t i = 0;

        while (i < described.choices.size() &&
               described.choices[i].first != valueText)
            i++;

        if (i < described.choices.size())
            want = described.choices[i].second;
        else
        {
            double n = 0;
            const int at = thPanelNumberIn(valueText, n)
                           ? thPanelChoiceIndex(described.choices, n) : -1;

            /* A value the plugin does not implement has no row and is
               refused rather than corrected -- the same answer the panel
               gives when it finds one already stored: show nothing, rather
               than a name that is not what is there. */
            if (at < 0)
                return thPanelResult::refuse(row + " has no value called " +
                                             valueText);

            want = described.choices[at].second;
        }
    }
    else
    {
        double shown = 0;

        if (!thPanelNumberIn(valueText, shown))
            return thPanelResult::refuse(valueText + " is not a number");

        /* Held to the range the control declares.
         *
         * The desktop has always done this, in the Gtk::Adjustment a slider
         * and its value box share, and the rule belongs with the parameter
         * rather than with the widget that happened to enforce it -- a
         * number typed into the page has the same travel to stay inside. A
         * control's min and max are its declared travel, which is a
         * different thing from a composer param's, where they are a hint to
         * whoever draws the knob and clamping to them breaks pieces. */
        double held = shown;

        if (described.hi >= described.lo)
        {
            if (held < described.lo) held = described.lo;
            if (held > described.hi) held = described.hi;
        }

        /* And held to the row's resolution, by going out through the
         * spelling the row would show it as and back.
         *
         * `step' is ten to the minus `decimals', so a number finer than that
         * is one no control can display or hand back -- the invariant
         * thPanelRow::step states. The desktop rounded as a side effect of
         * the value box being a SpinButton; a number posted from the page
         * arrives here having been near no such thing. Rounding before the
         * clamp would be the wrong way round on a range whose own end is
         * finer than its step, so the clamp goes again after it. */
        double rounded = 0;

        if (thPanelNumberIn(thPanelSpell(held, described.decimals), rounded))
            held = rounded;

        if (described.hi >= described.lo)
        {
            if (held < described.lo) held = described.lo;
            if (held > described.hi) held = described.hi;
        }

        want = thPanelFromDisplay(held, described.units);
    }

    out.value = want;

    /* The catching-up guard. See thPanelResult.
     *
     * Compared at the width deliver() writes, not the width propose()
     * computed in. The arg holds a float; `want' is a double folded back out
     * of a decimal spelling, and 0.3 as a double is not 0.3 as a float. The
     * two widths compared against each other make every edit a change that
     * never lands -- a control returned to the value it already holds would
     * mark the patch dirty, and the page would re-post the broadcast on
     * every echo of its own edit, for ever. */
    if ((*arg)[0] == (float)want)
        return thPanelResult::echo();

    return thPanelResult();
}

bool ArgPanel::deliver (const thPanelEdit &edit) const
{
    thArg *arg = argFor(edit.row);

    /* The same question propose() asks, asked again at the write.
     *
     * Not redundant: these are two calls and on the page they happen on two
     * machines. An intent arrives here as a command off the wire, and the
     * peer applying it has run no propose() of its own -- so a row this
     * panel does not offer has to be refused here as well, or the check is
     * one the sender could simply not have made. */
    if (!offers(edit.row, arg))
        return false;

    /* A single-float write, which is safe from the GUI thread while the
       audio thread reads: no reallocation, and the store is atomic. This is
       how every slider has always reached the graph. */
    arg->setValue((float)edit.value);

    return true;
}
