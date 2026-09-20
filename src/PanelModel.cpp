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

#include "think.h"
#include "thUnits.h"

#include "PanelModel.h"

int thPanel::indexOf (const string &id) const
{
    for (size_t i = 0; i < rows.size(); i++)
        if (rows[i].id == id)
            return (int)i;

    return -1;
}

const thPanelRow *thPanel::find (const string &id) const
{
    const int at = indexOf(id);

    return (at < 0) ? NULL : &rows[at];
}

thPanelResult thPanelResult::refuse (const string &why)
{
    thPanelResult r;

    r.ok = false;
    r.changed = false;
    r.why = why;

    return r;
}

thPanelResult thPanelResult::echo (void)
{
    thPanelResult r;

    r.changed = false;

    return r;
}

/* The rate the loader folded at. See the header. */
static long displayRate (void)
{
    thSynth *synth = thSynth::instance();

    return synth ? synth->getSampleRate() : TH_SAMPLE;
}

double thPanelToDisplay (double raw, const string &units)
{
    return thUnfoldUnit(raw, units, displayRate());
}

double thPanelFromDisplay (double shown, const string &units)
{
    return thFoldUnit(shown, units, displayRate());
}

int thPanelDecimals (double hi)
{
    const double m = fabs(hi);

    if (m >= 1000) return 0;
    if (m >= 100)  return 1;
    if (m >= 10)   return 2;

    return 4;
}

int thPanelValueChars (double hi, int decimals)
{
    double m = fabs(hi);
    int intDigits = 1;

    while (m >= 10) { m /= 10; intDigits++; }

    /* integer part, the point and its decimals, and one for a minus sign */
    const int chars = intDigits + (decimals ? decimals + 1 : 0) + 1;

    return chars < 6 ? 6 : chars;
}

string thPanelSpell (double value, int decimals)
{
    char buf[64];

    if (decimals < 0)
        decimals = 0;
    else if (decimals > 12)
        decimals = 12;

    /* %f rather than %g: a panel's numbers line up in a column, and an
       exponent in the middle of one is unreadable. A range wide enough for
       %g to reach for one has no decimals by then anyway. */
    snprintf(buf, sizeof buf, "%.*f", decimals, value);

    /* -0 is a value nothing means and every rounding of a small negative
       produces. */
    if (buf[0] == '-' && strspn(buf + 1, "0.") == strlen(buf + 1))
        return buf + 1;

    return buf;
}

int thPanelChoiceIndex (const vector<pair<string, int> > &choices,
                        double value)
{
    const int want = (int)value;

    for (size_t i = 0; i < choices.size(); i++)
        if (choices[i].second == want)
            return (int)i;

    return -1;
}

thPanelBuilder::thPanelBuilder (void)
{
}

void thPanelBuilder::add (const thPanelRow &row, const string &declared,
                          const string &inferred)
{
    rows_.push_back(row);
    groups_.push_back(declared.empty() ? inferred : declared);
}

/* Structure, hashed: what a shell has to rebuild its widgets for.
 *
 * Everything a row *holds* is left out on purpose -- a value, a spelling, a
 * selected choice. Those move constantly and pushing them into the widgets
 * that already exist is the whole reason this number is separate from the
 * panel. What is in it is what a widget is made of: which rows there are, in
 * what order, drawn how, whether each is offered or only shown, and the
 * numbers a control is built out of.
 *
 * That last is easy to leave out and wrong to: a slider's travel, its step
 * and the width of its value box are all cut into the widget when it is
 * made, and none of them can be pushed into one afterwards. Two patches
 * whose rows have the same names and differ only in `.max' are two different
 * panels, and a shell told they were the same would leave a 0..1 four-decimal
 * slider standing in front of a parameter that runs to 2000. */
static void hashInto (unsigned &h, const string &s)
{
    for (size_t i = 0; i < s.size(); i++)
    {
        h ^= (unsigned char)s[i];
        h *= 16777619u;
    }

    h ^= '\n';
    h *= 16777619u;
}

static void hashInto (unsigned &h, int n)
{
    hashInto(h, to_string(n));
}

/* Spelled at full precision rather than hashed as bytes. %.17g is the same
   seventeen digits for the same double under either toolchain and tells two
   different doubles apart, which the layout of one's bytes is not something
   to assume about a second compiler. */
static void hashInto (unsigned &h, double v)
{
    char buf[40];

    snprintf(buf, sizeof(buf), "%.17g", v);

    hashInto(h, string(buf));
}

void thPanelBuilder::finish (thPanel &panel) const
{
    vector<string> order;   /* groups, in the order they were first seen */
    vector<int> count;      /* how many rows each has */

    for (size_t i = 0; i < rows_.size(); i++)
    {
        if (groups_[i].empty())
            continue;

        size_t g = 0;

        while (g < order.size() && order[g] != groups_[i])
            g++;

        if (g == order.size())
        {
            order.push_back(groups_[i]);
            count.push_back(0);
        }

        count[g]++;
    }

    /* Each row's group after the groups of one have been dissolved, which is
       what the rest of this works from. */
    vector<string> kept(rows_.size());

    for (size_t i = 0; i < rows_.size(); i++)
        for (size_t g = 0; g < order.size(); g++)
            if (order[g] == groups_[i] && count[g] > 1)
                kept[i] = groups_[i];

    panel.groupOrder.clear();
    panel.rows.clear();

    for (size_t g = 0; g < order.size(); g++)
        if (count[g] > 1)
            panel.groupOrder.push_back(order[g]);

    /* Loose first, then each surviving group's rows together. A row whose
       group was dissolved keeps its place among the loose ones rather than
       being appended after them, so a group of one does not reorder the
       panel around it. */
    for (int pass = -1; pass < (int)panel.groupOrder.size(); pass++)
    {
        const string want = (pass < 0) ? string() : panel.groupOrder[pass];

        for (size_t i = 0; i < rows_.size(); i++)
        {
            if (kept[i] != want)
                continue;

            thPanelRow row = rows_[i];

            row.group = want;

            panel.rows.push_back(row);
        }
    }

    unsigned h = 2166136261u;

    for (size_t i = 0; i < panel.rows.size(); i++)
    {
        const thPanelRow &row = panel.rows[i];

        hashInto(h, row.id);
        hashInto(h, (int)row.kind);
        hashInto(h, row.group);
        hashInto(h, row.label);
        hashInto(h, row.units);
        hashInto(h, row.editable ? 1 : 0);
        hashInto(h, row.knob);
        hashInto(h, row.lo);
        hashInto(h, row.hi);
        hashInto(h, row.step);
        hashInto(h, row.decimals);
        hashInto(h, row.valueChars);
        hashInto(h, (int)row.choices.size());

        for (size_t c = 0; c < row.choices.size(); c++)
        {
            hashInto(h, row.choices[c].first);
            hashInto(h, row.choices[c].second);
        }
    }

    for (size_t i = 0; i < panel.actions.size(); i++)
    {
        hashInto(h, panel.actions[i].id);
        hashInto(h, panel.actions[i].enabled ? 1 : 0);
    }

    panel.shape = h;
}
