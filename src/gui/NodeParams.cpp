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

#include <gtkmm.h>

#include "think.h"

#include "../NodeGraph.h"
#include "NodeParams.h"

NodeParams::NodeParams (void)
    : graph_(NULL), box_(-1), grid_(NULL), loading_(false)
{
    set_size_request(240, -1);

    title_.set_alignment(Gtk::ALIGN_START, Gtk::ALIGN_CENTER);
    title_.set_padding(8, 4);

    subtitle_.set_alignment(Gtk::ALIGN_START, Gtk::ALIGN_CENTER);
    subtitle_.set_padding(8, 0);

    scroller_.set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);

    pack_start(title_, Gtk::PACK_SHRINK);
    pack_start(subtitle_, Gtk::PACK_SHRINK);
    pack_start(scroller_);

    setBox(NULL, -1);
}

/* Renders one parameter.
 *
 * The four kinds want visibly different treatment. A plain value is editable.
 * The other three are not numbers at all -- they are references to somewhere
 * the value comes from -- and showing them as greyed-out text rather than as a
 * disabled spin button makes it obvious that the thing to change is the wire,
 * not the field. */
void NodeParams::addRow (Gtk::Grid *grid, int row, const NodeGraph::Param &p)
{
    string caption = p.label.empty() ? p.name : p.label;

    Gtk::Label *name = manage(new Gtk::Label(caption));

    name->set_alignment(Gtk::ALIGN_START, Gtk::ALIGN_CENTER);
    name->set_padding(8, 2);

    if (!p.label.empty() && p.label != p.name)
        name->set_tooltip_text(p.name);

    if (!p.comment.empty())
        name->set_tooltip_text(p.comment);

    grid->attach(*name, 0, row, 1, 1);

    if (p.kind != NodeGraph::Param::VALUE || p.isOutput)
    {
        string text = p.source;
        string tip;

        if (p.isOutput && p.kind == NodeGraph::Param::VALUE)
        {
            /* Shown as a number, but not offered for editing: the plugin
               writes it on every window. Before anything has been rendered it
               reads 0, which is honest rather than interesting. */
            char buf[64];

            snprintf(buf, sizeof(buf), "%g", p.value);

            text = buf;
            tip = "An output of this node. The plugin writes it.";
        }
        else
            switch (p.kind)
            {
                case NodeGraph::Param::POINTER:
                    tip = "Driven by another node. "
                          "Change the wire, not the value.";
                    break;

                case NodeGraph::Param::NOTE:
                    /* Not the same thing as a channel arg, and saying so
                       matters: a note arg is per-note -- velocity, the note
                       number -- and changing the channel would not touch it. */
                    tip = "Comes from the note being played.";
                    break;

                default:
                    tip = "Comes from a channel parameter.";
                    break;
            }

        Gtk::Label *src = manage(new Gtk::Label(text));

        src->set_alignment(Gtk::ALIGN_START, Gtk::ALIGN_CENTER);
        src->set_padding(8, 2);
        src->set_sensitive(false);
        src->set_tooltip_text(tip);

        grid->attach(*src, 1, row, 1, 1);

        return;
    }

    /* A range the .dsp never gave would make a spin button that clamps to
       zero, so fall back to something wide enough to be no constraint. The
       current value has to fit regardless of what the .dsp claimed -- some
       files set a value outside their own declared range. */
    double lo = p.min, hi = p.max;

    if (lo == 0.0 && hi == 0.0)
    {
        lo = -100000.0;
        hi =  100000.0;
    }

    if (p.value < lo) lo = p.value;
    if (p.value > hi) hi = p.value;

    Glib::RefPtr<Gtk::Adjustment> adj =
        Gtk::Adjustment::create(p.value, lo, hi, 0.01, 1.0);

    Gtk::SpinButton *spin = manage(new Gtk::SpinButton(adj, 0.01, 4));

    spin->set_numeric(true);
    spin->set_hexpand(true);

    if (!p.units.empty())
        spin->set_tooltip_text("in " + p.units);

    /* Commit on Enter or on focus leaving, not on every increment: a spin
       button emits value_changed once per arrow click, and each of those would
       otherwise become a separate edit to the file. */
    spin->signal_activate().connect(
        sigc::bind(sigc::mem_fun(*this, &NodeParams::onSpinActivate),
                   spin, p.name));

    spin->signal_focus_out_event().connect(
        sigc::bind_return(
            sigc::hide(sigc::bind(
                sigc::mem_fun(*this, &NodeParams::onSpinActivate),
                spin, p.name)),
            false));

    grid->attach(*spin, 1, row, 1, 1);
}

void NodeParams::onSpinActivate (Gtk::SpinButton *spin, string name)
{
    if (loading_ || box_ < 0)
        return;

    spin->update();     /* take what was typed, not the last committed value */

    m_signal_param_edited_(box_, name, spin->get_value());
}

void NodeParams::setBox (const NodeGraph *graph, int box)
{
    loading_ = true;

    graph_ = graph;
    box_ = box;

    if (grid_)
    {
        scroller_.remove();
        delete grid_;
        grid_ = NULL;
    }

    if (graph == NULL || box < 0 || box >= (int)graph->boxes().size())
    {
        title_.set_markup("<b>No node selected</b>");
        subtitle_.set_text("Click a node to see its parameters.");

        loading_ = false;
        return;
    }

    const NodeGraph::Box &b = graph->boxes()[box];

    title_.set_markup("<b>" + Glib::Markup::escape_text(b.name) + "</b>");
    subtitle_.set_text(b.plugin);

    grid_ = new Gtk::Grid;

    grid_->set_row_spacing(2);
    grid_->set_column_spacing(6);

    int row = 0;

    for (size_t i = 0; i < b.params.size(); i++)
        addRow(grid_, row++, b.params[i]);

    if (row == 0)
    {
        Gtk::Label *none = manage(new Gtk::Label("No parameters."));

        none->set_padding(8, 8);
        none->set_sensitive(false);

        grid_->attach(*none, 0, 0, 2, 1);
    }

    scroller_.add(*grid_);
    grid_->show_all();

    loading_ = false;
}
