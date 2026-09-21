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

#include <stdio.h>
#include <stdlib.h>

#include <cmath>

#include "NodeGraph.h"
#include "NodePanel.h"

NodePanel::NodePanel (void)
    : graph_(NULL), box_(-1)
{
}

void NodePanel::setBox (const NodeGraph *graph, int box)
{
    graph_ = graph;
    box_ = box;
}

static const NodeGraph::Box *boxOf (const NodeGraph *graph, int box)
{
    if (graph == NULL || box < 0 || box >= (int)graph->boxes().size())
        return NULL;

    return &graph->boxes()[(size_t)box];
}

/* True for a parameter a person may type a number into: a value of its own,
   not written by the plugin, and one the file actually gives. */
static bool offered (const NodeGraph::Param &p)
{
    return p.kind == NodeGraph::Param::VALUE && !p.isOutput && p.hasValue;
}

bool NodePanel::settable (const NodeGraph *graph, int box)
{
    const NodeGraph::Box *b = boxOf(graph, box);

    if (b == NULL)
        return false;

    for (size_t i = 0; i < b->params.size(); i++)
        if (offered(b->params[i]))
            return true;

    return false;
}

/* A plain number, spelled the way the file does.
 *
 * `%g' and not the panel's own spelling: what is shown here is what a
 * .dsp holds, and the number goes back into one. A value written 0.5
 * should read 0.5 and not 0.5000. */
static string plainly (double v)
{
    char buf[64];

    snprintf(buf, sizeof buf, "%g", v);

    return buf;
}

/* What the tooltip says, in increasing order of how much it is worth
 * reading, with the last one present winning.
 *
 * The arg's own name is a fallback for when the caption is a label that
 * hides it -- the shell supplies that one, since an empty desc falls back to
 * the row's id. The plugin's description is what the plugin author wrote
 * about that arg; the .dsp's own comment beats it, because it is about this
 * patch rather than about the plugin in general, and whoever wrote it was
 * looking at this node when they did.
 *
 * Then what the plugin says the arg is defined over, which is not the row's
 * lo and hi: those are a control's travel and a node arg has none. It is the
 * only thing that tells filt::moog's cutoff (0 to 1, a fraction of the rate)
 * from filt::res2pole2's (hertz). */
static string describe (const NodeGraph::Param &p, const string &why)
{
    string desc = p.desc;

    if (!p.comment.empty())
        desc = p.comment;

    if (!why.empty())
        desc = desc.empty() ? why : desc + "  " + why;

    if (p.hasRange)
    {
        char span[80];

        snprintf(span, sizeof span, "%g to %g", p.rangeMin, p.rangeMax);

        desc = desc.empty() ? span : desc + "  (" + span + ")";
    }

    return desc;
}

static thPanelRow rowFor (const NodeGraph::Param &p)
{
    thPanelRow row;

    row.id = p.name;
    row.label = p.label.empty() ? p.name : p.label;
    row.units = p.units;
    row.value = p.value;

    if (p.isOutput && p.kind == NodeGraph::Param::VALUE)
    {
        /* Shown as a number, and not offered: the plugin writes it on every
           window. Before anything has been rendered it reads 0, which is
           honest rather than interesting. */
        row.kind = thPanelRow::READONLY;
        row.editable = false;
        row.text = p.hasValue ? plainly(p.value) : string();
        row.desc = describe(p, "An output of this node. The plugin writes "
                               "it.");

        return row;
    }

    if (p.kind != NodeGraph::Param::VALUE)
    {
        row.kind = thPanelRow::READONLY;
        row.editable = false;
        row.text = p.source;

        switch (p.kind)
        {
            case NodeGraph::Param::POINTER:
                row.desc = describe(p, "Driven by another node. Change the "
                                       "wire, not the value.");
                break;

            /* Not the same thing as a channel arg, and saying so matters: a
               note arg is per-note -- velocity, the note number -- and
               changing the channel would not touch it. */
            case NodeGraph::Param::NOTE:
                row.desc = describe(p, "Comes from the note being played.");
                break;

            /* A name, not a number, so there is nothing here a slider or a
               controller could do. `source' already carries it with its
               quotes on, which is what the file says. */
            case NodeGraph::Param::TEXT:
                row.desc = describe(p, "A name the plugin resolves. Edit it "
                                       "in the file.");
                break;

            /* In most DSPs every setting worth touching is a chanarg, so
               showing only "@cutoff" would be a list of names with no
               numbers in it. Editing one means rewriting the channel block
               rather than this node -- and that is the channel's own panel,
               which is a different panel over a different thing. */
            default:
                /* Bare: `source' is the spelling and carries its `@',
                   which stays in the text below; this is the name a lookup
                   would use. */
                row.knob = p.source.compare(0, 1, "@") == 0
                    ? p.source.substr(1) : p.source;
                row.desc = describe(p, "Comes from a channel parameter.");

                if (p.hasValue)
                    row.text = p.source + " = " + plainly(p.value);
                break;
        }

        return row;
    }

    /* A plain number the file gives, and the only kind anyone may type
       into. */
    row.desc = describe(p, "");
    row.editable = p.hasValue;

    /* A range the .dsp never gave would make a box that clamps to zero, so
       fall back to something wide enough to be no constraint. The value has
       to fit regardless of what the .dsp claimed -- some files set one
       outside their own declared range. */
    row.lo = p.min;
    row.hi = p.max;

    if (row.lo == 0 && row.hi == 0)
    {
        row.lo = -100000;
        row.hi = 100000;
    }

    /* A parameter the plugin reads as a whole number steps by one and takes
       no decimals. `waveform' is `switch ((int)x)': a box offering 3.4
       offers a triangle spelled in a way that suggests it is not one. */
    const bool whole = (p.step == 1);

    for (size_t i = 0; i < p.valueNames.size(); i++)
        if (!p.valueNames[i].empty())
            row.choices.push_back(make_pair(p.valueNames[i], (int)i));

    if (whole && !row.choices.empty())
    {
        /* Where every value has a name, the travel is the list and not
           whatever the .dsp declared. Eight shipped patches say
           `.max = 5.1' for six waveforms -- padding for a slider that could
           not otherwise reach the last one -- and honouring that would
           offer a seventh position that does nothing. */
        row.kind = thPanelRow::CHOICE;
        row.lo = row.choices.front().second;
        row.hi = row.choices.back().second;
        row.step = 1;
        row.decimals = 0;
        row.valueChars = thPanelValueChars(row.hi, 0);

        const int at = thPanelChoiceIndex(row.choices, row.value);

        row.text = (at < 0) ? string() : row.choices[at].first;

        return row;
    }

    if (row.value < row.lo) row.lo = row.value;
    if (row.value > row.hi) row.hi = row.value;

    /* A number box and no slider. A node arg's min and max are not a
       control's travel -- most args have none and get the fallback above --
       so a slider would be a handle sweeping a range nobody declared. */
    row.kind = thPanelRow::NUMBER;
    row.decimals = whole ? 0 : 4;
    row.step = pow(10.0, -row.decimals);
    row.valueChars = thPanelValueChars(row.hi, row.decimals);
    row.text = plainly(row.value);

    return row;
}

bool NodePanel::build (thPanel &out) const
{
    out = thPanel();
    out.kind = thPanel::NODE_VALUE;
    out.a = box_;

    const NodeGraph::Box *b = boxOf(graph_, box_);

    if (b == NULL)
        return false;

    /* Which node, and what it is: a panel over one box of thirty has to say
       which one, and the plugin's spelling is how a reader knows what the
       rows mean. */
    out.title = b->name;
    out.subtitle = b->plugin;

    thPanelBuilder build;

    for (size_t i = 0; i < b->params.size(); i++)
        build.add(rowFor(b->params[i]), "", "");

    build.finish(out);

    return !out.rows.empty();
}

thPanelResult NodePanel::propose (const string &row, const string &valueText,
                                  thPanelEdit &out) const
{
    out = thPanelEdit();
    out.kind = thPanelEdit::NODE_VALUE;
    out.row = row;
    out.valueText = valueText;
    out.a = box_;

    const NodeGraph::Box *b = boxOf(graph_, box_);

    if (b == NULL)
        return thPanelResult::refuse("no such node");

    size_t at = 0;

    while (at < b->params.size() && b->params[at].name != row)
        at++;

    if (at == b->params.size())
        return thPanelResult::refuse("no parameter called " + row);

    const NodeGraph::Param &p = b->params[at];

    /* A shown row is not an offered one, and a panel drawn before the graph
       was rewired may still have one. The thing to change is the wire. */
    if (!offered(p))
        return thPanelResult::refuse(row + " is not a value this node owns");

    const thPanelRow described = rowFor(p);

    double want = 0;

    if (described.kind == thPanelRow::CHOICE)
    {
        size_t i = 0;

        while (i < described.choices.size() &&
               described.choices[i].first != valueText)
            i++;

        if (i == described.choices.size())
            return thPanelResult::refuse(row + " has no value called " +
                                         valueText);

        want = described.choices[i].second;
    }
    else
    {
        /* The model's, not a copy: this one asked whether a digit had been
           read only after skipping the trailing blanks, which takes "   "
           for a good 0 and splices it into the .dsp. */
        if (!thPanelNumberIn(valueText, want))
            return thPanelResult::refuse(valueText + " is not a number");
    }

    out.value = want;

    /* The catching-up guard, and here it does a second job: a panel is
       rebuilt from the file after every splice, so a row reporting the
       value it was just given would splice the same line again. */
    if (p.value == want)
        return thPanelResult::echo();

    return thPanelResult();
}
