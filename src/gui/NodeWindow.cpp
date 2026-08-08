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
#include <string.h>

#include <gtkmm.h>

#include "think.h"

#include "../NodeGraph.h"
#include "../NodeLayout.h"
#include "../NodeEdit.h"
#include "NodeCanvas.h"
#include "NodeParams.h"
#include "NodeWindow.h"

NodeWindow::NodeWindow (thSynth *synth)
    : synth_(synth), tree_(NULL), layoutDirty_(false),
      arrangeBtn_("Auto-arrange"), saveBtn_("Save"), revertBtn_("Revert"),
      zoomInBtn_("+"), zoomOutBtn_("-"), zoomResetBtn_("1:1")
{
    set_title("thinksynth - Nodes");
    set_default_size(900, 600);

    add(vbox_);

    toolbar_.set_spacing(4);
    toolbar_.set_border_width(4);
    toolbar_.pack_start(arrangeBtn_, Gtk::PACK_SHRINK);
    toolbar_.pack_start(saveBtn_, Gtk::PACK_SHRINK);
    toolbar_.pack_start(revertBtn_, Gtk::PACK_SHRINK);
    toolbar_.pack_end(zoomInBtn_, Gtk::PACK_SHRINK);
    toolbar_.pack_end(zoomResetBtn_, Gtk::PACK_SHRINK);
    toolbar_.pack_end(zoomOutBtn_, Gtk::PACK_SHRINK);

    scroller_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    scroller_.add(canvas_);

    split_.pack1(scroller_, true, false);
    split_.pack2(params_, false, false);
    split_.set_position(660);

    status_.set_alignment(Gtk::ALIGN_START, Gtk::ALIGN_CENTER);
    status_.set_padding(6, 2);

    vbox_.pack_start(toolbar_, Gtk::PACK_SHRINK);
    vbox_.pack_start(split_);
    vbox_.pack_start(status_, Gtk::PACK_SHRINK);

    arrangeBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &NodeWindow::onArrange));
    saveBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &NodeWindow::onSave));
    revertBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &NodeWindow::onRevert));
    zoomInBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &NodeWindow::onZoomIn));
    zoomOutBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &NodeWindow::onZoomOut));
    zoomResetBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &NodeWindow::onZoomReset));

    canvas_.signal_box_moved().connect(
        sigc::mem_fun(*this, &NodeWindow::onBoxMoved));

    canvas_.signal_selected().connect(
        sigc::mem_fun(*this, &NodeWindow::onSelected));

    canvas_.signal_connect_requested().connect(
        sigc::mem_fun(*this, &NodeWindow::onConnect));

    canvas_.signal_disconnect_requested().connect(
        sigc::mem_fun(*this, &NodeWindow::onDisconnect));

    canvas_.signal_refused().connect(
        sigc::mem_fun(*this, &NodeWindow::onRefused));

    params_.signal_param_edited().connect(
        sigc::mem_fun(*this, &NodeWindow::onParamEdited));

    saveBtn_.set_sensitive(false);
    revertBtn_.set_sensitive(false);

    show_all_children();
}

NodeWindow::~NodeWindow (void)
{
    /* parseTree() handed us a tree nobody else tracks. */
    delete tree_;
}

void NodeWindow::setStatus (const string &text)
{
    status_.set_text(text);
}

void NodeWindow::updateTitle (void)
{
    string base = thUtil::basename((char *)filename_.c_str());

    const bool dirty = layoutDirty_ || !pending_.empty() || !wires_.empty();

    set_title("thinksynth - Nodes - " + base + (dirty ? " *" : ""));
}

void NodeWindow::updateDirty (void)
{
    const bool dirty = layoutDirty_ || !pending_.empty() || !wires_.empty();

    /* Save is offered only when there is something to save. It used to be
       enabled whenever a file was open, and saving with nothing pending still
       wrote the layout block -- so opening a .dsp that had never been through
       the editor and pressing Save added twenty comment lines to a file the
       user had not edited. */
    saveBtn_.set_sensitive(!filename_.empty() && dirty);
    revertBtn_.set_sensitive(dirty);

    updateTitle();
}

bool NodeWindow::open (const string &filename)
{
    thSynthTree *tree = synth_->parseTree(filename);

    if (tree == NULL)
    {
        setStatus("Could not parse " + filename);
        return false;
    }

    NodeGraph g;

    if (!g.build(tree))
    {
        delete tree;
        setStatus("Could not build a graph for " + filename);
        return false;
    }

    g.layout();

    /* Saved positions override the computed ones, per box. A file with a
       partial layout block -- a node added by hand since the last save -- gets
       the laid-out position for the ones it does not mention, which is more
       useful than either ignoring the file or piling the strays at 0,0. */
    NodeLayout::PosMap pos;
    int restored = 0;

    if (NodeLayout::read(filename, pos) && !pos.empty())
        restored = NodeLayout::apply(g, pos, g);

    /* Swap in only once everything above has succeeded, so a failed open
       leaves the previous file on screen rather than a blank canvas. */
    delete tree_;
    tree_ = tree;
    graph_ = g;
    filename_ = filename;
    layoutDirty_ = false;
    pending_.clear();
    wires_.clear();

    canvas_.setGraph(&graph_);
    params_.setBox(NULL, -1);
    updateDirty();     /* leaves Save insensitive: nothing is pending yet */

    char buf[256];

    snprintf(buf, sizeof(buf),
             "%d nodes, %d connections (%d feedback), %d layers%s",
             (int)graph_.boxes().size(), (int)graph_.edges().size(),
             graph_.feedbackCount(), graph_.layerCount(),
             restored ? ", layout restored" : "");

    setStatus(buf);

    return true;
}

void NodeWindow::onArrange (void)
{
    if (tree_ == NULL)
        return;

    graph_.layout();
    canvas_.setGraph(&graph_);

    /* Auto-arranging discards hand placement, so it is itself an edit. */
    layoutDirty_ = true;
    updateDirty();

    setStatus("Auto-arranged.");
}

/* Writes the pending values first, then the positions.
 *
 * Values first because NodeLayout::write rewrites the whole file to move its
 * comment block, and doing that between two value splices would mean the
 * second splice worked from a file the first had already reflowed. */
bool NodeWindow::writeAll (string &why)
{
    /* Wires before values: disconnecting puts a plain number in place of the
       connection, and a value edit on that same arg should land on top of it
       rather than being overwritten by it. */
    for (size_t w = 0; w < wires_.size(); w++)
    {
        const WireEdit &e = wires_[w];

        NodeEdit::Result r =
            e.srcNode.empty()
                ? NodeEdit::disconnect(filename_, e.node, e.arg, 0, why)
                : NodeEdit::connect(filename_, e.node, e.arg,
                                    e.srcNode, e.srcPort, why);

        if (r != NodeEdit::OK)
        {
            if (why.empty())
                why = string(NodeEdit::resultText(r));

            why = e.node + "." + e.arg + ": " + why;

            return false;
        }
    }

    for (std::map<std::pair<std::string, std::string>, double>::const_iterator
             i = pending_.begin();
         i != pending_.end(); ++i)
    {
        NodeEdit::Result r = NodeEdit::setValue(filename_, i->first.first,
                                                i->first.second, i->second,
                                                why);

        if (r != NodeEdit::OK)
        {
            if (why.empty())
                why = string(NodeEdit::resultText(r));

            why = i->first.first + "." + i->first.second + ": " + why;

            return false;
        }
    }

    if (!NodeLayout::write(filename_, graph_))
    {
        why = "could not write the layout";
        return false;
    }

    return true;
}

void NodeWindow::onSave (void)
{
    if (filename_.empty())
        return;

    const int n = (int)pending_.size();
    const int w = (int)wires_.size();

    string why;

    if (!writeAll(why))
    {
        Gtk::MessageDialog dlg(*this, "Could not save.", false,
                               Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
        dlg.set_secondary_text(why);
        dlg.run();

        setStatus("Not saved: " + why);
        return;
    }

    /* Reopen rather than trusting the in-memory graph. The file is now the
       truth, and reparsing it is the only way to see what it actually says --
       including a value the writer had to round to something spellable. */
    const string f = filename_;
    const int sel = canvas_.selected();

    if (!open(f))
        return;

    if (sel >= 0 && sel < (int)graph_.boxes().size())
        canvas_.setSelected(sel);

    char buf[160];

    /* One snprintf per case rather than a format string chosen by a
       conditional. The earlier version handed the same argument list to both
       branches, so the one taking no arguments read an int as a %s. */
    if (n || w)
        snprintf(buf, sizeof(buf),
                 "Saved: %d value%s, %d wire change%s, and the layout.",
                 n, n == 1 ? "" : "s", w, w == 1 ? "" : "s");
    else
        snprintf(buf, sizeof(buf), "Saved: layout only.");

    setStatus(buf);
}

void NodeWindow::onRevert (void)
{
    if (filename_.empty())
        return;

    pending_.clear();
    wires_.clear();
    layoutDirty_ = false;

    open(filename_);

    setStatus("Reverted to what is on disk.");
}

void NodeWindow::onBoxMoved (int box)
{
    (void)box;

    if (layoutDirty_)
        return;

    layoutDirty_ = true;
    updateDirty();
}

void NodeWindow::onSelected (int box)
{
    params_.setBox(&graph_, box);
}

/* Records an edit. Nothing reaches the file until Save.
 *
 * Keyed by node and arg name rather than by box index so it survives the
 * reparse a save does. */
void NodeWindow::onParamEdited (int box, string name, double value)
{
    if (box < 0 || box >= (int)graph_.boxes().size())
        return;

    const NodeGraph::Box &b = graph_.boxes()[box];

    /* Typing a value back to what it already was is not an edit, and should
       not leave the window looking modified. */
    for (size_t k = 0; k < b.params.size(); k++)
        if (b.params[k].name == name &&
            b.params[k].hasValue && (float)b.params[k].value == (float)value)
        {
            pending_.erase(make_pair(b.name, name));
            updateDirty();
            setStatus("");
            return;
        }

    pending_[make_pair(b.name, name)] = value;

    updateDirty();

    char buf[256];

    snprintf(buf, sizeof(buf), "%s.%s = %g  (%d unsaved change%s)",
             b.name.c_str(), name.c_str(), value,
             (int)pending_.size(), pending_.size() == 1 ? "" : "s");

    setStatus(buf);
}

/* Applies a wire to the graph on screen and records it for the save.
 *
 * Applied immediately rather than only on Save so the canvas shows what was
 * just drawn -- a wiring gesture that produced no visible wire until a save
 * would be unusable. The file is still untouched until Save. */
void NodeWindow::onConnect (int fromBox, int fromPort, int toBox, int toPort)
{
    string why;

    if (!graph_.connect(fromBox, fromPort, toBox, toPort, why))
    {
        setStatus("Cannot connect: " + why);
        return;
    }

    WireEdit e;

    e.node = graph_.boxes()[toBox].name;
    e.arg = graph_.boxes()[toBox].ports[toPort].name;
    e.srcNode = graph_.boxes()[fromBox].name;
    e.srcPort = graph_.boxes()[fromBox].ports[fromPort].name;

    wires_.push_back(e);

    canvas_.queue_draw();
    params_.setBox(&graph_, canvas_.selected());
    updateDirty();

    setStatus(e.node + "." + e.arg + " <- " + e.srcNode + "->" + e.srcPort);
}

void NodeWindow::onDisconnect (int edge)
{
    if (edge < 0 || edge >= (int)graph_.edges().size())
        return;

    const NodeGraph::Edge &ed = graph_.edges()[edge];

    WireEdit e;

    e.node = graph_.boxes()[ed.toBox].name;
    e.arg = graph_.boxes()[ed.toBox].ports[ed.toPort].name;

    graph_.removeEdge(edge);

    wires_.push_back(e);

    canvas_.queue_draw();
    params_.setBox(&graph_, canvas_.selected());
    updateDirty();

    setStatus("Disconnected " + e.node + "." + e.arg + ".  Revert undoes it.");
}

void NodeWindow::onRefused (string why)
{
    setStatus("Cannot connect: " + why);
}

void NodeWindow::onZoomIn (void)
{
    canvas_.setZoom(canvas_.zoom() * 1.25);
}

void NodeWindow::onZoomOut (void)
{
    canvas_.setZoom(canvas_.zoom() / 1.25);
}

void NodeWindow::onZoomReset (void)
{
    canvas_.setZoom(1.0);
}
