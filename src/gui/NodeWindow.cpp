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
#include "NodeCanvas.h"
#include "NodeWindow.h"

NodeWindow::NodeWindow (thSynth *synth)
    : synth_(synth), tree_(NULL), dirty_(false),
      arrangeBtn_("Auto-arrange"), saveBtn_("Save layout"),
      zoomInBtn_("+"), zoomOutBtn_("-"), zoomResetBtn_("1:1")
{
    set_title("thinksynth - Nodes");
    set_default_size(900, 600);

    add(vbox_);

    toolbar_.set_spacing(4);
    toolbar_.set_border_width(4);
    toolbar_.pack_start(arrangeBtn_, Gtk::PACK_SHRINK);
    toolbar_.pack_start(saveBtn_, Gtk::PACK_SHRINK);
    toolbar_.pack_end(zoomInBtn_, Gtk::PACK_SHRINK);
    toolbar_.pack_end(zoomResetBtn_, Gtk::PACK_SHRINK);
    toolbar_.pack_end(zoomOutBtn_, Gtk::PACK_SHRINK);

    scroller_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    scroller_.add(canvas_);

    status_.set_alignment(Gtk::ALIGN_START, Gtk::ALIGN_CENTER);
    status_.set_padding(6, 2);

    vbox_.pack_start(toolbar_, Gtk::PACK_SHRINK);
    vbox_.pack_start(scroller_);
    vbox_.pack_start(status_, Gtk::PACK_SHRINK);

    arrangeBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &NodeWindow::onArrange));
    saveBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &NodeWindow::onSaveLayout));
    zoomInBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &NodeWindow::onZoomIn));
    zoomOutBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &NodeWindow::onZoomOut));
    zoomResetBtn_.signal_clicked().connect(
        sigc::mem_fun(*this, &NodeWindow::onZoomReset));

    canvas_.signal_box_moved().connect(
        sigc::mem_fun(*this, &NodeWindow::onBoxMoved));

    saveBtn_.set_sensitive(false);

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

    set_title("thinksynth - Nodes - " + base + (dirty_ ? " *" : ""));
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
    dirty_ = false;

    canvas_.setGraph(&graph_);
    saveBtn_.set_sensitive(true);
    updateTitle();

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
    dirty_ = true;
    updateTitle();

    setStatus("Auto-arranged.");
}

void NodeWindow::onSaveLayout (void)
{
    if (filename_.empty())
        return;

    if (!NodeLayout::write(filename_, graph_))
    {
        setStatus("Could not write layout to " + filename_);
        return;
    }

    dirty_ = false;
    updateTitle();

    setStatus("Layout saved. Only `# @layout' comments were changed.");
}

void NodeWindow::onBoxMoved (int box)
{
    (void)box;

    if (dirty_)
        return;

    dirty_ = true;
    updateTitle();
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
