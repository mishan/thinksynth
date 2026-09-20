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

#include "KnobPanel.h"

KnobPanel::KnobPanel (void)
{
}

void KnobPanel::setKnobs (const std::vector<thArg *> &knobs)
{
    knobs_ = knobs;
}

/* A row id is an index, so this is the whole of resolving one. -1 for a row
   that names no knob, which is what a stale intent carries. */
static int indexIn (const std::vector<thArg *> &knobs, const string &row)
{
    if (row.empty())
        return -1;

    for (size_t i = 0; i < row.size(); i++)
        if (row[i] < '0' || row[i] > '9')
            return -1;

    const long at = strtol(row.c_str(), NULL, 10);

    return (at >= 0 && at < (long)knobs.size()) ? (int)at : -1;
}

thArg *KnobPanel::argFor (const string &row) const
{
    const int at = indexIn(knobs_, row);

    return at < 0 ? NULL : knobs_[at];
}

/* One knob, described.
 *
 * Always a slider: a knob is a number a hand moves while the piece plays,
 * and a piece that named its values would be declaring a selector rather
 * than a knob. The range is the piece's own -- `@warmth 0.5 0 1' -- and the
 * resolution follows it, which is what knobs.js could only guess at with
 * toPrecision(3): three significant figures on a knob running to 20000 is
 * three digits of a five-digit number. */
static thPanelRow rowFor (thArg *knob, int index)
{
    thPanelRow row;

    row.kind = thPanelRow::SLIDER;
    row.id = to_string(index);
    row.label = knob->label().empty() ? knob->name() : knob->label();

    /* The name it is written as, which is what a person reading the .gen
       is looking for and is not always what the label says. In `knob' as
       well as in the tooltip: a command from a peer names a knob by its
       number and a preset names it by this, and a shell holding the rows
       should not have to take the `@' off a tooltip to get it. */
    row.knob = knob->name();
    row.desc = "@" + knob->name();

    /* No units. A knob is declared as a bare range and is folded by
       nothing, so there is nothing to unfold -- and a stage reading one
       reads the number it holds. */
    row.lo = knob->min();
    row.hi = knob->max();
    row.decimals = (knob->step() == 1) ? 0 : thPanelDecimals(row.hi);
    row.step = pow(10.0, -row.decimals);
    row.valueChars = thPanelValueChars(row.hi, row.decimals);
    row.value = (*knob)[0];
    row.text = thPanelSpell(row.value, row.decimals);

    return row;
}

bool KnobPanel::build (thPanel &out) const
{
    out = thPanel();
    out.kind = thPanel::KNOB;

    thPanelBuilder build;

    for (size_t i = 0; i < knobs_.size(); i++)
    {
        /* A knob the piece marked hidden keeps its number and gets no row.
           The numbering is the module's, over every knob declared, and a
           list that closed the gap would send a command to the wrong
           knob. */
        if (knobs_[i] == NULL ||
            knobs_[i]->widgetType() == thArg::HIDE)
            continue;

        build.add(rowFor(knobs_[i], (int)i), knobs_[i]->group(), "");
    }

    build.finish(out);

    return !out.rows.empty();
}

bool KnobPanel::valueFor (const string &row, double &display) const
{
    thArg *knob = argFor(row);

    if (knob == NULL)
        return false;

    display = (*knob)[0];

    return true;
}

thPanelResult KnobPanel::propose (const string &row, const string &valueText,
                                  thPanelEdit &out) const
{
    out = thPanelEdit();
    out.kind = thPanelEdit::KNOB;
    out.row = row;
    out.valueText = valueText;

    const int at = indexIn(knobs_, row);

    if (at < 0)
        return thPanelResult::refuse("no knob numbered " + row);

    out.a = at;

    const char *s = valueText.c_str();
    char *end = NULL;

    const double typed = strtod(s, &end);

    while (*end == ' ' || *end == '\t')
        end++;

    if (end == s || *end != '\0' || !std::isfinite(typed))
        return thPanelResult::refuse(valueText + " is not a number");

    /* Held to the range the piece declared, which is the travel its slider
       has. A stage reading past it is reading a number the author never
       offered. */
    const thPanelRow described = rowFor(knobs_[at], at);

    double want = typed;

    if (described.hi >= described.lo)
    {
        if (want < described.lo) want = described.lo;
        if (want > described.hi) want = described.hi;
    }

    out.value = want;

    /* The catching-up guard. See thPanelResult. */
    if ((double)(*knobs_[at])[0] == want)
        return thPanelResult::echo();

    return thPanelResult();
}

bool KnobPanel::deliver (const thPanelEdit &edit) const
{
    thArg *knob = argFor(edit.row);

    if (knob == NULL)
        return false;

    knob->setValue((float)edit.value);

    return true;
}
