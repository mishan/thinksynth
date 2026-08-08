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

#include <gtkmm.h>

#include "think.h"

#include "NodeCanvas.h"

/* Palette. Kept flat and low-contrast so the wires stay the loudest thing on
   screen -- in a signal-flow diagram the connections are the content. */
#define COL_BG        0.16, 0.17, 0.19
#define COL_BOX       0.24, 0.25, 0.28
#define COL_BOX_EDGE  0.38, 0.40, 0.44
#define COL_HEAD      0.30, 0.32, 0.36
#define COL_IO_HEAD   0.24, 0.36, 0.44
#define COL_TEXT      0.88, 0.89, 0.91
#define COL_DIM       0.62, 0.64, 0.68
#define COL_WIRE      0.55, 0.72, 0.85
#define COL_FEEDBACK  0.88, 0.55, 0.40
#define COL_PORT_IN   0.60, 0.75, 0.55
#define COL_PORT_OUT  0.80, 0.72, 0.50

#define PORT_R  3.5

NodeCanvas::NodeCanvas (void)
    : graph_(NULL)
{
}

void NodeCanvas::setGraph (NodeGraph *graph)
{
    graph_ = graph;

    if (graph_)
        set_size_request((int)graph_->width(), (int)graph_->height());

    queue_draw();
}

void NodeCanvas::drawBox (const Cairo::RefPtr<Cairo::Context> &cr,
                          const NodeGraph::Box &b)
{
    cr->set_line_width(1.0);

    /* body */
    cr->rectangle(b.x + 0.5, b.y + 0.5, b.w, b.h);
    cr->set_source_rgb(COL_BOX);
    cr->fill_preserve();
    cr->set_source_rgb(COL_BOX_EDGE);
    cr->stroke();

    /* title bar -- the io halves get their own colour, since "midi in" and
       "audio out" are the two ends of the signal path and worth spotting */
    cr->rectangle(b.x + 0.5, b.y + 0.5, b.w, 20.0);
    if (b.isIoSource || b.isIoSink)
        cr->set_source_rgb(COL_IO_HEAD);
    else
        cr->set_source_rgb(COL_HEAD);
    cr->fill();

    cr->select_font_face("sans", Cairo::FONT_SLANT_NORMAL,
                         Cairo::FONT_WEIGHT_BOLD);
    cr->set_font_size(10.0);
    cr->set_source_rgb(COL_TEXT);
    cr->move_to(b.x + 6, b.y + 14);
    cr->show_text(b.name);

    if (!b.plugin.empty())
    {
        cr->select_font_face("sans", Cairo::FONT_SLANT_ITALIC,
                             Cairo::FONT_WEIGHT_NORMAL);
        cr->set_font_size(8.0);
        cr->set_source_rgb(COL_DIM);

        Cairo::TextExtents te;
        cr->get_text_extents(b.plugin, te);
        cr->move_to(b.x + b.w - te.width - 6, b.y + 14);
        cr->show_text(b.plugin);
    }

    /* ports */
    cr->select_font_face("sans", Cairo::FONT_SLANT_NORMAL,
                         Cairo::FONT_WEIGHT_NORMAL);
    cr->set_font_size(9.0);

    for (size_t k = 0; k < b.ports.size(); k++)
    {
        const NodeGraph::Port &p = b.ports[k];

        cr->arc(b.x + p.x, b.y + p.y, PORT_R, 0, 2 * M_PI);
        cr->set_source_rgb(p.isInput ? COL_PORT_IN : COL_PORT_OUT);
        cr->fill();

        cr->set_source_rgb(COL_DIM);

        if (p.isInput)
        {
            cr->move_to(b.x + p.x + PORT_R + 4, b.y + p.y + 3);
            cr->show_text(p.name);
        }
        else
        {
            Cairo::TextExtents te;
            cr->get_text_extents(p.name, te);
            cr->move_to(b.x + p.x - PORT_R - 4 - te.width, b.y + p.y + 3);
            cr->show_text(p.name);
        }
    }
}

void NodeCanvas::drawEdge (const Cairo::RefPtr<Cairo::Context> &cr,
                           const NodeGraph::Edge &e)
{
    const vector<NodeGraph::Box> &boxes = graph_->boxes();

    const NodeGraph::Box &fb = boxes[e.fromBox];
    const NodeGraph::Box &tb = boxes[e.toBox];

    const double x1 = fb.x + fb.ports[e.fromPort].x;
    const double y1 = fb.y + fb.ports[e.fromPort].y;
    const double x2 = tb.x + tb.ports[e.toPort].x;
    const double y2 = tb.y + tb.ports[e.toPort].y;

    /* Horizontal-tangent bezier: wires leave an output rightwards and enter an
       input from the left, which reads as flow even where a wire doubles back.
       The control offset grows with distance so long wires bow more. */
    double reach = (x2 - x1) * 0.5;

    if (reach < 30.0)
        reach = 30.0 + (x1 - x2) * 0.25;    /* a back edge needs a wider bow */

    cr->move_to(x1, y1);
    cr->curve_to(x1 + reach, y1, x2 - reach, y2, x2, y2);

    if (e.feedback)
    {
        cr->set_source_rgb(COL_FEEDBACK);
        cr->set_line_width(1.6);

        vector<double> dashes;
        dashes.push_back(4.0);
        dashes.push_back(3.0);
        cr->set_dash(dashes, 0.0);
    }
    else
    {
        cr->set_source_rgb(COL_WIRE);
        cr->set_line_width(1.3);
        cr->unset_dash();
    }

    cr->stroke();
    cr->unset_dash();
}

bool NodeCanvas::on_draw (const Cairo::RefPtr<Cairo::Context> &cr)
{
    Gtk::Allocation alloc = get_allocation();

    cr->set_source_rgb(COL_BG);
    cr->rectangle(0, 0, alloc.get_width(), alloc.get_height());
    cr->fill();

    if (graph_ == NULL)
        return true;

    /* Wires first so boxes sit on top of them; a wire disappearing behind a
       box reads better than one crossing its face. */
    const vector<NodeGraph::Edge> &edges = graph_->edges();

    for (size_t e = 0; e < edges.size(); e++)
        drawEdge(cr, edges[e]);

    const vector<NodeGraph::Box> &boxes = graph_->boxes();

    for (size_t b = 0; b < boxes.size(); b++)
        drawBox(cr, boxes[b]);

    return true;
}
