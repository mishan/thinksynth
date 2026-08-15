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
    : Gtk::Box(Gtk::Orientation::VERTICAL),
      graph_(NULL), box_(-1), grid_(NULL), loading_(false)
{
    set_size_request(240, -1);

    title_.set_xalign(0.0);
    title_.set_margin_start(8);
    title_.set_margin_end(8);
    title_.set_margin_top(4);
    title_.set_margin_bottom(4);

    subtitle_.set_xalign(0.0);
    subtitle_.set_margin_start(8);
    subtitle_.set_margin_end(8);

    scroller_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);

    append(title_);
    append(subtitle_);
    scroller_.set_vexpand(true);
    append(scroller_);

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

    name->set_xalign(0.0);
    name->set_margin_start(8);
    name->set_margin_end(8);
    name->set_margin_top(2);
    name->set_margin_bottom(2);

    if (!p.label.empty() && p.label != p.name)
        name->set_tooltip_text(p.name);

    if (!p.comment.empty())
        name->set_tooltip_text(p.comment);

    grid->attach(*name, 0, row, 1, 1);

    if (p.kind != NodeGraph::Param::VALUE || p.isOutput)
    {
        string text = p.source;
        string tip;

        char num[64] = "";

        if (p.hasValue)
            snprintf(num, sizeof(num), "%g", p.value);

        if (p.isOutput && p.kind == NodeGraph::Param::VALUE)
        {
            /* Shown as a number, but not offered for editing: the plugin
               writes it on every window. Before anything has been rendered it
               reads 0, which is honest rather than interesting. */
            text = num;
            tip = "An output of this node. The plugin writes it.";
        }
        else if (p.kind == NodeGraph::Param::POINTER)
            tip = "Driven by another node. Change the wire, not the value.";
        else if (p.kind == NodeGraph::Param::NOTE)
        {
            /* Not the same thing as a channel arg, and saying so matters: a
               note arg is per-note -- velocity, the note number -- and
               changing the channel would not touch it. */
            tip = "Comes from the note being played.";
        }
        else
        {
            /* In most DSPs every setting worth touching is a chanarg, so
               showing only "@cutoff" would be a list of names with no numbers
               in it. Editing one means rewriting the channel block rather than
               this node, which is not built yet -- hence shown, not offered. */
            if (p.hasValue)
                text = p.source + " = " + num;

            tip = "Comes from a channel parameter.";

            if (!p.units.empty())
                tip += "  (" + p.units + ")";
        }

        Gtk::Label *src = manage(new Gtk::Label(text));

        src->set_xalign(0.0);
        src->set_margin_start(8);
        src->set_margin_end(8);
        src->set_margin_top(2);
        src->set_margin_bottom(2);
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

    /* A parameter the plugin reads as a whole number steps by one and takes no
       decimals. `waveform' is `switch ((int)x)': a spin button offering 3.4
       offers a triangle spelled in a way that suggests it is not one. */
    const bool whole = (p.step == 1);

    if (whole)
    {
        /* And where every value has a name, the range is the list rather than
           whatever the .dsp declared. Eight shipped patches say `.max = 5.1'
           for six waveforms -- padding for a slider that could not otherwise
           reach the last one -- and honouring that here would offer a seventh
           position that does nothing. */
        if (!p.valueNames.empty())
        {
            lo = 0;
            hi = (double)p.valueNames.size() - 1;

            if (p.value < lo) lo = p.value;
            if (p.value > hi) hi = p.value;
        }
    }

    Glib::RefPtr<Gtk::Adjustment> adj =
        Gtk::Adjustment::create(p.value, lo, hi, whole ? 1 : 0.01, 1.0);

    Gtk::SpinButton *spin = manage(new Gtk::SpinButton(adj, whole ? 1 : 0.01,
                                                       whole ? 0 : 4));

    spin->set_numeric(true);
    spin->set_hexpand(true);

    /* One tooltip, built from everything there is to say and set once.
     *
     * Both halves used to call set_tooltip_text, so on an arg with a unit *and*
     * named values the unit replaced the names -- and the names are the half
     * that cannot be worked out by looking at the number. */
    string tip;

    if (!p.units.empty())
        tip = "in " + p.units;

    /* The names beside the number rather than instead of it.
     *
     * A dropdown would be the better widget, and it is what the overview panel
     * and the canvas strip both use -- but this column is a grid of spin
     * buttons whose edits commit on Enter or on focus leaving, and one row
     * behaving differently is a worse trade here than a tooltip. The number is
     * also the thing being written to the .dsp, so seeing it is not useless. */
    for (size_t i = 0; i < p.valueNames.size(); i++)
    {
        if (p.valueNames[i].empty())
            continue;           /* a value the plugin does not implement */

        char n[16];

        snprintf(n, sizeof(n), "%d = ", (int)i);

        if (!tip.empty())
            tip += "\n";

        tip += n + p.valueNames[i];
    }

    if (!tip.empty())
        spin->set_tooltip_text(tip);

    /* Commit on Enter or on focus leaving, not on every increment: a spin
       button emits value_changed once per arrow click, and each of those
       would otherwise become a separate edit to the file.
     *
     * Both halves are controllers now. Focus leaving was an event on the
       widget; Enter came from Gtk::Entry, which a GTK4 spin button is not one
       of any more -- it implements Gtk::Editable instead, and grew a
       signal_activate() of its own only in gtkmm 4.14. Ubuntu 24.04 ships
       exactly 4.14, so relying on that would have been relying on the newest
       thing the oldest supported runner has. A key controller works on every
       GTK4 and says plainly which key is meant. */
    Glib::RefPtr<Gtk::EventControllerKey> keys =
        Gtk::EventControllerKey::create();

    keys->signal_key_pressed().connect(
        sigc::bind(sigc::mem_fun(*this, &NodeParams::onSpinKey), spin, p.name),
        false);

    spin->add_controller(keys);

    Glib::RefPtr<Gtk::EventControllerFocus> focus =
        Gtk::EventControllerFocus::create();

    focus->signal_leave().connect(
        sigc::bind(sigc::mem_fun(*this, &NodeParams::onSpinActivate),
                   spin, p.name));

    spin->add_controller(focus);

    grid->attach(*spin, 1, row, 1, 1);
}

/* Enter, and nothing else. False for every other key so the spin button's own
   handling -- the arrows, digits, tab -- is untouched. */
bool NodeParams::onSpinKey (guint keyval, guint keycode,
                            Gdk::ModifierType state, Gtk::SpinButton *spin,
                            string name)
{
    (void)keycode; (void)state;

    if (keyval != GDK_KEY_Return && keyval != GDK_KEY_KP_Enter)
        return false;

    onSpinActivate(spin, name);

    return true;
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
        scroller_.unset_child();
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

        none->set_margin_start(8);
        none->set_margin_end(8);
        none->set_margin_top(8);
        none->set_margin_bottom(8);
        none->set_sensitive(false);

        grid_->attach(*none, 0, 0, 2, 1);
    }

    scroller_.set_child(*grid_);

    loading_ = false;
}
