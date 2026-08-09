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
#define COL_SELECT    0.95, 0.85, 0.45
#define COL_ATTACH    0.27, 0.25, 0.33
#define COL_CUT       0.90, 0.42, 0.38
#define COL_CTL_HEAD  0.34, 0.30, 0.44
#define COL_TRACK     0.16, 0.17, 0.19
#define COL_FILL      0.62, 0.55, 0.82
#define COL_HANDLE    0.86, 0.83, 0.94

#define PORT_R  3.5

#define ZOOM_MIN  0.25
#define ZOOM_MAX  3.0

NodeCanvas::NodeCanvas (void)
    : graph_(NULL), zoom_(1.0), dragBox_(-1), dragDX_(0), dragDY_(0),
      hoverBox_(-1), hoverPort_(-1), selBox_(-1),
      wireBox_(-1), wirePort_(-1), wireX_(0), wireY_(0),
      wireTargetBox_(-1), wireTargetPort_(-1), wireTargetOk_(false),
      hoverEdge_(-1), dragSlider_(-1)
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
    dragBox_ = hoverBox_ = hoverPort_ = selBox_ = -1;
    wireBox_ = wirePort_ = wireTargetBox_ = wireTargetPort_ = -1;
    hoverEdge_ = -1;
    dragSlider_ = -1;

    m_signal_selected_(-1);

    updateSize();
    queue_draw();
}

void NodeCanvas::setSelected (int box)
{
    if (box == selBox_)
        return;

    selBox_ = box;

    m_signal_selected_(selBox_);
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

    /* A slider takes precedence over the box it is drawn on, or a control
       could never be adjusted -- only dragged around. */
    const int slider = graph_->sliderAt(gx, gy);

    if (slider >= 0)
    {
        dragSlider_ = slider;

        const double v = graph_->sliderValueAt(slider, gx);

        graph_->setControlValue(slider, (float)v);

        setSelected(slider);
        m_signal_control_(slider, v, false);
        queue_draw();

        return true;
    }

    /* A port takes precedence over the box it sits on: the handles straddle
       the edge, so a click there means "wire", not "move". */
    int pb = -1, pp = -1;

    if (graph_->portAt(gx, gy, pb, pp))
    {
        wireBox_ = pb;
        wirePort_ = pp;
        wireX_ = gx;
        wireY_ = gy;
        wireTargetBox_ = wireTargetPort_ = -1;
        wireTargetOk_ = false;

        setSelected(pb);
        queue_draw();

        return true;
    }

    /* Clicking a wire and pressing Delete is one interaction too many for
       something this small, so a click on a wire removes it. The window
       confirms nothing -- Revert is right there, and nothing has been written
       to the file yet. */
    const int edge = graph_->edgeAt(gx, gy);

    if (edge >= 0 && graph_->boxAt(gx, gy) < 0)
    {
        m_signal_disconnect_(edge);
        return true;
    }

    const int hit = graph_->boxAt(gx, gy);

    /* Selecting on press rather than on release: a drag should show you what
       you are dragging while you drag it. */
    setSelected(hit);

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

    if (dragSlider_ >= 0)
    {
        const int s = dragSlider_;

        dragSlider_ = -1;

        /* The committing emit. Everything before this was live feedback. */
        m_signal_control_(s, (double)graph_->boxes()[s].ctlValue, true);

        queue_draw();

        return true;
    }

    if (wireBox_ >= 0)
    {
        const int fromBox = wireBox_, fromPort = wirePort_;
        const int toBox = wireTargetBox_, toPort = wireTargetPort_;

        wireBox_ = wirePort_ = -1;
        wireTargetBox_ = wireTargetPort_ = -1;

        queue_draw();

        if (toBox < 0)
            return true;        /* dropped on nothing; no complaint needed */

        /* Dragging output-to-input and input-to-output are the same gesture,
           so whichever end is the input becomes the destination. */
        int a = fromBox, ap = fromPort, z = toBox, zp = toPort;

        if (graph_->boxes()[a].ports[ap].isInput)
        {
            a = toBox; ap = toPort;
            z = fromBox; zp = fromPort;
        }

        string why;

        if (!graph_->canConnect(a, ap, z, zp, why))
        {
            m_signal_refused_(why);
            return true;
        }

        m_signal_connect_(a, ap, z, zp);

        return true;
    }

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

    if (dragSlider_ >= 0)
    {
        const double v = graph_->sliderValueAt(dragSlider_, gx);

        graph_->setControlValue(dragSlider_, (float)v);

        m_signal_control_(dragSlider_, v, false);
        queue_draw();

        return true;
    }

    if (wireBox_ >= 0)
    {
        wireX_ = gx;
        wireY_ = gy;

        int tb = -1, tp = -1;

        wireTargetOk_ = false;

        if (graph_->portAt(gx, gy, tb, tp) &&
            !(tb == wireBox_ && tp == wirePort_))
        {
            /* Test the connection in the direction it would actually be made,
               so the feedback while dragging matches what the drop will do. */
            int a = wireBox_, ap = wirePort_, z = tb, zp = tp;

            if (graph_->boxes()[a].ports[ap].isInput)
            { a = tb; ap = tp; z = wireBox_; zp = wirePort_; }

            string why;

            wireTargetOk_ = graph_->canConnect(a, ap, z, zp, why);
        }
        else
            tb = tp = -1;

        wireTargetBox_ = tb;
        wireTargetPort_ = tp;

        queue_draw();

        return true;
    }

    /* Hover feedback: ports, so the wire handles are discoverable, and wires,
       so it is clear which one a click would remove. */
    int hb = -1, hp = -1;

    graph_->portAt(gx, gy, hb, hp);

    int he = -1;

    if (hb < 0 && graph_->boxAt(gx, gy) < 0)
        he = graph_->edgeAt(gx, gy);

    if (hb != hoverBox_ || hp != hoverPort_ || he != hoverEdge_)
    {
        hoverBox_ = hb;
        hoverPort_ = hp;
        hoverEdge_ = he;
        queue_draw();
    }

    return true;
}

bool NodeCanvas::on_leave_notify_event (GdkEventCrossing *c)
{
    (void)c;

    if (hoverBox_ >= 0 || hoverPort_ >= 0 || hoverEdge_ >= 0)
    {
        hoverBox_ = hoverPort_ = hoverEdge_ = -1;
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

/* An attached control: label, track, number, on one line against its host.
 *
 * No title bar, no port, no outline to speak of -- it is not a node and
 * should not look like one. The only chrome is a tab on the right edge, which
 * is what says which box it belongs to now that no wire does. */
void NodeCanvas::drawAttached (const Cairo::RefPtr<Cairo::Context> &cr,
                               const NodeGraph::Box &b, bool highlit,
                               bool selected)
{
    cr->set_line_width(1.0);

    cr->rectangle(b.x + 0.5, b.y + 0.5, b.w, b.h);
    cr->set_source_rgb(COL_ATTACH);
    cr->fill_preserve();

    if (selected)
    {
        cr->set_source_rgb(COL_SELECT);
        cr->set_line_width(2.0);
        cr->stroke();
        cr->set_line_width(1.0);
    }
    else if (highlit)
    {
        cr->set_source_rgb(COL_WIRE);
        cr->stroke();
    }
    else
        cr->begin_new_path();

    /* The tab: a short bar on the right edge, pointing at the host. */
    cr->rectangle(b.x + b.w - 2.0, b.y + 3.0, 3.0, b.h - 6.0);
    cr->set_source_rgb(COL_FILL);
    cr->fill();

    cr->select_font_face("sans", Cairo::FONT_SLANT_NORMAL,
                         Cairo::FONT_WEIGHT_NORMAL);
    cr->set_font_size(9.0);
    cr->set_source_rgb(COL_TEXT);

    /* The label, clipped to its share of the strip so a long one cannot run
       under the track. */
    cr->save();
    cr->rectangle(b.x + 4.0, b.y, b.w * 0.42 - 8.0, b.h);
    cr->clip();
    cr->move_to(b.x + 5.0, b.y + b.h * 0.5 + 3.0);
    cr->show_text(b.ctlLabel);
    cr->restore();

    drawSlider(cr, b);
}

void NodeCanvas::drawBox (const Cairo::RefPtr<Cairo::Context> &cr,
                          const NodeGraph::Box &b, bool highlit, bool selected)
{
    if (b.attachedTo >= 0)
    {
        drawAttached(cr, b, highlit, selected);
        return;
    }

    cr->set_line_width(1.0);

    /* body */
    cr->rectangle(b.x + 0.5, b.y + 0.5, b.w, b.h);
    cr->set_source_rgb(COL_BOX);
    cr->fill_preserve();
    if (selected)
    {
        cr->set_source_rgb(COL_SELECT);
        cr->set_line_width(2.5);
    }
    else if (highlit)
    {
        cr->set_source_rgb(COL_WIRE);
        cr->set_line_width(2.0);
    }
    else
        cr->set_source_rgb(COL_BOX_EDGE);
    cr->stroke();
    cr->set_line_width(1.0);

    /* title bar -- the io halves and the controls get their own colours, since
       "midi in", "audio out" and the knobs are the parts of a patch you look
       for first */
    cr->rectangle(b.x + 0.5, b.y + 0.5, b.w, 20.0);
    if (b.isIoSource || b.isIoSink)
        cr->set_source_rgb(COL_IO_HEAD);
    else if (b.isControl)
        cr->set_source_rgb(COL_CTL_HEAD);
    else
        cr->set_source_rgb(COL_HEAD);
    cr->fill();

    cr->select_font_face("sans", Cairo::FONT_SLANT_NORMAL,
                         Cairo::FONT_WEIGHT_BOLD);
    cr->set_font_size(10.0);
    cr->set_source_rgb(COL_TEXT);
    cr->move_to(b.x + 6, b.y + 14);

    /* A control's label is what the .dsp author called it -- "Band Limit"
       rather than "blim" -- and that is the useful thing to read. The bare
       name goes in the right-hand corner where a node shows its plugin, so
       the box still says which `@name' it is: the label is for reading, the
       name is what the rest of the file refers to. */
    cr->show_text(b.isControl ? b.ctlLabel : b.name);

    if (b.isControl)
    {
        drawSlider(cr, b);

        /* The one output port still wants drawing, so fall through to the
           port loop rather than returning here. */
    }

    string corner = b.isControl ? ("@" + b.ctlArg) : b.plugin;

    /* A control still drawn as a box is one that several nodes share -- the
       rest are strips against the thing they drive. Saying how many says why
       this one is different, instead of leaving it looking like a control
       that failed to attach. */
    if (b.isControl)
    {
        int consumers = 0;

        for (size_t e = 0; e < graph_->edges().size(); e++)
            if (graph_->edges()[e].fromBox == (int)(&b - &graph_->boxes()[0]))
                consumers++;

        if (consumers > 1)
        {
            char buf[32];

            snprintf(buf, sizeof(buf), "  shared x%d", consumers);
            corner += buf;
        }
    }

    if (!corner.empty())
    {
        cr->select_font_face("sans", Cairo::FONT_SLANT_ITALIC,
                             Cairo::FONT_WEIGHT_NORMAL);
        cr->set_font_size(8.0);
        cr->set_source_rgb(COL_DIM);

        Cairo::TextExtents te;
        cr->get_text_extents(corner, te);
        cr->move_to(b.x + b.w - te.width - 6, b.y + 14);
        cr->show_text(corner);
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

/* The slider on a control box: a track, the filled part up to the handle, the
   handle, and the value.
 *
 * The geometry comes from NodeGraph::sliderGeometry, which is also what
 * sliderAt() and sliderValueAt() use, so what is drawn and what is dragged are
 * one thing. */
void NodeCanvas::drawSlider (const Cairo::RefPtr<Cairo::Context> &cr,
                             const NodeGraph::Box &b)
{
    /* The index by pointer arithmetic rather than by scanning. drawBox is
       handed a reference to an element of the vector, so the offset is exact;
       scanning for it made drawing the controls quadratic in the number of
       boxes, every frame, on the one code path that runs during a drag. */
    const vector<NodeGraph::Box> &all = graph_->boxes();

    if (all.empty() || &b < &all[0] || &b > &all[all.size() - 1])
        return;

    const int index = (int)(&b - &all[0]);

    double x0, x1, y, hx;

    if (!graph_->sliderGeometry(index, x0, x1, y, hx))
        return;

    cr->set_line_width(3.0);
    cr->set_line_cap(Cairo::LINE_CAP_ROUND);

    cr->move_to(x0, y);
    cr->line_to(x1, y);
    cr->set_source_rgb(COL_TRACK);
    cr->stroke();

    if (hx > x0)
    {
        cr->move_to(x0, y);
        cr->line_to(hx, y);
        cr->set_source_rgb(COL_FILL);
        cr->stroke();
    }

    cr->set_line_cap(Cairo::LINE_CAP_BUTT);

    cr->arc(hx, y, (index == dragSlider_) ? 6.0 : 5.0, 0, 2 * M_PI);
    cr->set_source_rgb(COL_HANDLE);
    cr->fill();

    char buf[48];

    snprintf(buf, sizeof(buf), "%.4g", (double)b.ctlValue);

    cr->select_font_face("sans", Cairo::FONT_SLANT_NORMAL,
                         Cairo::FONT_WEIGHT_NORMAL);
    cr->set_font_size(8.0);
    cr->set_source_rgb(COL_DIM);

    Cairo::TextExtents te;

    if (b.attachedTo >= 0)
    {
        /* On one line: the number goes to the right of the track, where the
           strip has room, and the range is dropped. A strip is for reading at
           a glance and adjusting; the range is in the panel when wanted. */
        cr->move_to(x1 + 6.0, y + 3.0);
        cr->show_text(buf);

        return;
    }

    /* The number, right-aligned under the track. Four significant figures is
       enough for anything with a declared range and short enough to fit. */
    cr->get_text_extents(buf, te);
    cr->move_to(b.x + b.w - te.width - 6, b.y + b.h - 3);
    cr->show_text(buf);

    /* ...and the range at the left, so the slider's travel means something. */
    snprintf(buf, sizeof(buf), "%.3g-%.3g", (double)b.ctlMin, (double)b.ctlMax);

    cr->move_to(b.x + 6, b.y + b.h - 3);
    cr->show_text(buf);
}

void NodeCanvas::drawEdge (const Cairo::RefPtr<Cairo::Context> &cr, int edge,
                           bool highlit)
{
    const NodeGraph::Edge &e = graph_->edges()[edge];

    /* The curve comes from the graph, which is also what edgeAt() tests
       against -- so what is drawn and what can be clicked are the same shape
       by construction rather than by two functions agreeing. */
    double xs[4], ys[4];

    graph_->edgeCurve(edge, xs, ys);

    cr->move_to(xs[0], ys[0]);
    cr->curve_to(xs[1], ys[1], xs[2], ys[2], xs[3], ys[3]);

    if (highlit)
    {
        /* A wire under the pointer is about to be removed if clicked, so it
           gets the warning colour rather than a subtle emphasis. */
        cr->set_source_rgb(COL_CUT);
        cr->set_line_width(2.6);
        cr->unset_dash();
    }
    else if (e.feedback)
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

/* The wire being dragged, from its origin port to the pointer. */
void NodeCanvas::drawPendingWire (const Cairo::RefPtr<Cairo::Context> &cr)
{
    if (wireBox_ < 0)
        return;

    double x1, y1;

    graph_->portPos(wireBox_, wirePort_, x1, y1);

    double x2 = wireX_, y2 = wireY_;

    /* Snap the loose end to the port it would land on, so the wire visibly
       commits before the button comes up. */
    if (wireTargetBox_ >= 0)
        graph_->portPos(wireTargetBox_, wireTargetPort_, x2, y2);

    double reach = (x2 - x1) * 0.5;

    if (reach < 30.0)
        reach = 30.0 + (x1 - x2) * 0.25;

    cr->move_to(x1, y1);
    cr->curve_to(x1 + reach, y1, x2 - reach, y2, x2, y2);

    if (wireTargetBox_ >= 0 && !wireTargetOk_)
        cr->set_source_rgb(COL_CUT);        /* would be refused */
    else if (wireTargetBox_ >= 0)
        cr->set_source_rgb(COL_SELECT);     /* would connect */
    else
        cr->set_source_rgb(COL_DIM);        /* nothing under the pointer */

    cr->set_line_width(2.0);

    vector<double> dashes;
    dashes.push_back(5.0);
    dashes.push_back(3.0);
    cr->set_dash(dashes, 0.0);

    cr->stroke();
    cr->unset_dash();

    if (wireTargetBox_ >= 0)
    {
        cr->arc(x2, y2, PORT_R + 2.5, 0, 2 * M_PI);
        cr->set_line_width(1.5);
        cr->stroke();
    }
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
    for (size_t e = 0; e < graph_->edges().size(); e++)
    {
        /* The join between a strip and its host: they are touching, so a
           line between them would be a line to nowhere. */
        if (graph_->edgeIsImplied((int)e))
            continue;

        drawEdge(cr, (int)e, (int)e == hoverEdge_);
    }

    const vector<NodeGraph::Box> &boxes = graph_->boxes();

    for (size_t b = 0; b < boxes.size(); b++)
        drawBox(cr, boxes[b], (int)b == dragBox_ || (int)b == hoverBox_,
                (int)b == selBox_);

    /* On top of everything: it is the thing being manipulated. */
    drawPendingWire(cr);

    cr->restore();

    return true;
}
