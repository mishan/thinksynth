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

#define ZOOM_MIN  0.25
#define ZOOM_MAX  3.0

NodeCanvas::NodeCanvas (void)
    : graph_(NULL), zoom_(1.0), dragBox_(-1), dragDX_(0), dragDY_(0),
      hoverBox_(-1), hoverPort_(-1)
{
    add_events(Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK |
               Gdk::POINTER_MOTION_MASK | Gdk::SCROLL_MASK |
               Gdk::SMOOTH_SCROLL_MASK | Gdk::LEAVE_NOTIFY_MASK);
}

void NodeCanvas::updateSize (void)
{
    if (graph_)
        set_size_request((int)(graph_->width() * zoom_),
                         (int)(graph_->height() * zoom_));
}

void NodeCanvas::setGraph (NodeGraph *graph)
{
    graph_ = graph;
    dragBox_ = hoverBox_ = hoverPort_ = -1;

    updateSize();
    queue_draw();
}

void NodeCanvas::setZoom (double z)
{
    if (z < ZOOM_MIN) z = ZOOM_MIN;
    if (z > ZOOM_MAX) z = ZOOM_MAX;

    zoom_ = z;

    updateSize();
    queue_draw();
}

void NodeCanvas::toGraph (double sx, double sy, double &gx, double &gy) const
{
    gx = sx / zoom_;
    gy = sy / zoom_;
}

bool NodeCanvas::on_button_press_event (GdkEventButton *b)
{
    if (graph_ == NULL || b->button != 1)
        return false;

    double gx, gy;

    toGraph(b->x, b->y, gx, gy);

    const int hit = graph_->boxAt(gx, gy);

    if (hit >= 0)
    {
        dragBox_ = hit;
        dragDX_ = gx - graph_->boxes()[hit].x;
        dragDY_ = gy - graph_->boxes()[hit].y;
    }

    return true;
}

bool NodeCanvas::on_button_release_event (GdkEventButton *b)
{
    (void)b;    /* which button came up does not matter here */

    if (dragBox_ >= 0)
    {
        /* A box dragged past the old bounds needs the scrollable area to grow
           with it, or it becomes unreachable. */
        graph_->refreshExtent();
        updateSize();

        m_signal_box_moved_(dragBox_);
        dragBox_ = -1;
    }

    return true;
}

bool NodeCanvas::on_motion_notify_event (GdkEventMotion *m)
{
    if (graph_ == NULL)
        return false;

    double gx, gy;

    toGraph(m->x, m->y, gx, gy);

    if (dragBox_ >= 0)
    {
        double nx = gx - dragDX_;
        double ny = gy - dragDY_;

        /* Keep boxes out of negative space: the canvas has no origin offset,
           so anything dragged above or left of zero would be unreachable. */
        if (nx < 0) nx = 0;
        if (ny < 0) ny = 0;

        graph_->moveBox(dragBox_, nx, ny);
        queue_draw();

        return true;
    }

    /* Hover feedback on ports. Not used for anything yet, but it is what makes
       the click target discoverable once wiring exists, and it is the cheapest
       possible check that portAt() agrees with what is drawn. */
    int hb = -1, hp = -1;

    graph_->portAt(gx, gy, hb, hp);

    if (hb != hoverBox_ || hp != hoverPort_)
    {
        hoverBox_ = hb;
        hoverPort_ = hp;
        queue_draw();
    }

    return true;
}

bool NodeCanvas::on_leave_notify_event (GdkEventCrossing *c)
{
    (void)c;

    if (hoverBox_ >= 0 || hoverPort_ >= 0)
    {
        hoverBox_ = hoverPort_ = -1;
        queue_draw();
    }

    return true;
}

bool NodeCanvas::on_scroll_event (GdkEventScroll *s)
{
    /* Ctrl+wheel zooms; a bare wheel is left to the scrolled window, which is
       what people expect of a large canvas. */
    if ((s->state & GDK_CONTROL_MASK) == 0)
        return false;

    if (s->direction == GDK_SCROLL_UP)
        setZoom(zoom_ * 1.1);
    else if (s->direction == GDK_SCROLL_DOWN)
        setZoom(zoom_ / 1.1);
    else if (s->direction == GDK_SCROLL_SMOOTH && s->delta_y != 0)
        setZoom(zoom_ * (s->delta_y < 0 ? 1.1 : 1.0 / 1.1));

    return true;
}

void NodeCanvas::drawBox (const Cairo::RefPtr<Cairo::Context> &cr,
                          const NodeGraph::Box &b, bool highlit)
{
    cr->set_line_width(1.0);

    /* body */
    cr->rectangle(b.x + 0.5, b.y + 0.5, b.w, b.h);
    cr->set_source_rgb(COL_BOX);
    cr->fill_preserve();
    if (highlit)
    {
        cr->set_source_rgb(COL_WIRE);
        cr->set_line_width(2.0);
    }
    else
        cr->set_source_rgb(COL_BOX_EDGE);
    cr->stroke();
    cr->set_line_width(1.0);

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

        const bool hot = (hoverBox_ >= 0 &&
                          &graph_->boxes()[hoverBox_] == &b &&
                          (int)k == hoverPort_);

        cr->arc(b.x + p.x, b.y + p.y, hot ? PORT_R + 2.0 : PORT_R, 0, 2 * M_PI);

        /* Not `set_source_rgb(p.isInput ? COL_PORT_IN : COL_PORT_OUT)'.
         *
         * These macros are three comma-separated numbers, and the middle
         * operand of ?: swallows commas: that expression parsed as
         * set_source_rgb(isInput ? (0.60, 0.75, 0.55) : 0.80, 0.72, 0.50),
         * i.e. red from the conditional with green and blue always 0.72 and
         * 0.50. Inputs came out olive rather than green, and the two port
         * colours differed only in the red channel. */
        if (p.isInput)
            cr->set_source_rgb(COL_PORT_IN);
        else
            cr->set_source_rgb(COL_PORT_OUT);

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

    cr->save();
    cr->scale(zoom_, zoom_);

    /* Wires first so boxes sit on top of them; a wire disappearing behind a
       box reads better than one crossing its face. */
    const vector<NodeGraph::Edge> &edges = graph_->edges();

    for (size_t e = 0; e < edges.size(); e++)
        drawEdge(cr, edges[e]);

    const vector<NodeGraph::Box> &boxes = graph_->boxes();

    for (size_t b = 0; b < boxes.size(); b++)
        drawBox(cr, boxes[b], (int)b == dragBox_ || (int)b == hoverBox_);

    cr->restore();

    return true;
}
